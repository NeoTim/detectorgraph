// Copyright 2017 Nest Labs, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef DETECTORGRAPH_INCLUDE_GRAPH_HPP_
#define DETECTORGRAPH_INCLUDE_GRAPH_HPP_


#include "subscriberinterface.hpp"
#include "vertex.hpp"
#include "subscriptiondispatcher.hpp"
#include "graphinputdispatcher.hpp"
#include "topic.hpp"
#include "topicstate.hpp"
#include "topicregistry.hpp"
#include "graphinputqueue.hpp"

#include "errortype.hpp"

#if defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
// LITE_BEGIN
#include "detectorgraphliteconfig.hpp"
#include "sequencecontainer-lite.hpp"
// LITE_END
#else
// VANILLA_BEGIN
#include "sharedptr.hpp"
#include <list>
#include <typeinfo>
#include <limits>
#include <stdint.h>
// VANILLA_END
#endif

namespace DetectorGraph
{

class Detector;

/**
 * @brief Implements a graph of \link Topic Topics\endlink & \link Detector
 * Detectors\endlink with Input/Output APIs
 *
 * Application's Graphs can be built using Aggregation or Composition with this
 * class.
 *
 * Detectors are added to the graph and the graph is responsible for creating
 * the necessary Topics to satisfy all dependencies.
 *
 * Below are a couple of example of how to setup Graph with Detectors:
 *
 * - Simple / bare-bones:
 * @snippet helloworld.cpp Adding to a Graph
 * This is the simplest way to build graphs and is used throughout unit tests.
 *
 * - Aggregation example using automatic storage for graph & detectors:
 * @code
class MyApplication
{
public:
    MyApplication() : mGraph(), mFooDetector(&mGraph), mBarDetector(&mGraph)
    {
    }

    Graph mGraph;
    FooDetector mFooDetector;
    BarDetector mBarDetector;
};
 * @endcode
 *
 * - Composition using dynamic allocation of detectors:
 * @code
class MyApplicationGraph : public DetectorGraph::Graph
{
public:
    FooBarGraph()
    {
        mFooDetector = new FooDetector(this);
        mBarDetector = new BarDetector(this);
    }

    ~FooBarGraph()
    {
        delete mFooDetector;
        delete mBarDetector;
    }

    FooDetector* mFooDetector;
    BarDetector* mBarDetector;
};
 * @endcode
 * This option offers a lot of flexibility at construction time and makes graph
 * ownership of detectors more obvious.
 *
 *
 * Graph:
 * - Owns all vertices (Topics and Detectors)
 * - Provides the data input API (@ref PushData<T>) (into topics)
 * - Provides the data output API (@ref GetOutputList) (from topics)
 * - Provides an API for graph evaluation (@ref EvaluateGraph)
 * - Maintains the Graph's topological sort across graph changes topology changes
 * - Creates Topics as needed to satisfy all Detector's dependencies
 *
 * Typical control flow:
 * - External events/messages/inputs are translated into TopicStates and
 * passed to PushData().
 * - EvaluateGraph() running in an event loop until HasDataPending() is false.
 * - After each call to EvaluateGraph() the list in GetOutputList() is
 * inspected for TopicStates of interest that must be passed onwards to the
 * outside.
 *
 *
 * This class also maintains an Inversion of Control container (@ref TopicRegistry)
 * for Topics. The IoC container is basically a "repository" of singleton topics.
 */
class Graph
{
public:
    /**
     * @brief Constructor
     *
     * Does nothing really
     */
    Graph();

    /**
     * @brief Destructor
     *
     * When a Graph is destroyed, it deletes all the pending graph dispatchers,
     * detectors, and topics (even if they're not referred to by any detectors).
     */
    virtual ~Graph();

    /**
     * @brief Find/add a topic in the detector graph
     *
     * If the topic to resolve already exists, return it.
     * Otherwise, registering a new topic and then return it.
     *
     * \return A unique topic of type \p TTopic in this graph.
     */
    template<class TTopicState> Topic<TTopicState>* ResolveTopic()
    {
        Topic<TTopicState>* tObj = mTopicRegistry.Resolve<TTopicState>();

#if defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
        // LITE_BEGIN
        // Trying to resolve unregistered topic.
        DG_ASSERT(tObj != NULL);
        // LITE_BEGIN
#else
        // VANILLA_BEGIN
        // Creates Topics on demand.
        if (tObj == NULL)
        {
            tObj = new Topic<TTopicState>();
            mTopicRegistry.Register<TTopicState>(tObj);
            AddVertex(tObj);
        }
        // VANILLA_END
#endif

        return tObj;
    } // LCOV_EXCL_LINE

    /**
     * @brief Push data to a specific topic in the graph
     *
     * This method is used to input data into the graph and
     * it's the only API to do so.
     *
     */
    template<class TTopicState> void PushData(const TTopicState& aTopicState)
    {
        mGraphInputQueue.Enqueue(*ResolveTopic<TTopicState>(), aTopicState);
    }

    /**
     * @brief Evaluate the whole graph
     */
    ErrorType EvaluateGraph();

    /**
     * @brief Returns true if there is data pending evaluation
     *
     * This can be useful when implementing a 'flush & evaluate all data'
     * pattern because calls to \ref EvaluateGraph() remove only single TopicState
     * from the input queue at a time for each evaluation of the graph.
     */
    bool HasDataPending();

    /**
     * @brief Evaluates Graph if data is pending and returns true if so.
     *
     * This method is a convenience combination of HasDataPending() and
     * EvaluateGraph() that makes it a bit cleaner to implement simple
     * evaluation loops.
     * It assumes that EvaluateGraph() always succeeds; it'll fail a DG_ASSERT
     * otherwise.
     */
    bool EvaluateIfHasDataPending();

    void AddVertex(Vertex* aVertex);
    void RemoveVertex(Vertex* aVertex);
    TopicRegistry& GetTopicRegistry();

    size_t GetVerticesSize() const
    {
        return mVertices.size();
    }

private:
    /**
     * @ brief Clears @ref VertexSearchState to kVertexClear on all vertices
     */
    ErrorType ClearTraverseContexts();

    /**
     * @brief Traverse though vertices in the right order and perform computation
     */
    ErrorType TraverseVertices();

private:
    TopicRegistry mTopicRegistry;
    GraphInputQueue mGraphInputQueue;


#if defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
    // LITE_BEGIN
public:
    /**
     * @brief Checks that the vertices are topologically sorted.
     */
    bool IsGraphSorted();

private:
    SequenceContainer<Vertex*, DetectorGraphConfig::kMaxNumberOfVertices> mVertices;
    // LITE_END
#else
    // VANILLA_BEGIN

public:
    const std::list< Vertex* >& GetVertices() const;

    /**
     * @brief Returns the list of topicstates published in the last Evaluation.
     *
     * It returns a list of references (valid only in between evaluations) to
     * the topicstates that changed during the last call to \ref EvaluateGraph()
     */
    const std::list<ptr::shared_ptr<const TopicState> >& GetOutputList() const;

    /**
     * @brief Determine the right order to process the vertices by topological sort
     */
    ErrorType TopoSortGraph();

private:
    /**
     * @brief Pop data out of topics to the output list after evaluation
     */
    ErrorType ComposeOutputList();

    /**
     * @brief Recursive method used on Depth-First-Search used on topo-sorting
     */
    ErrorType DFS_visit(Vertex* v, std::list<Vertex*>& sorted);

private:
    bool mNeedsSorting;
    std::list< Vertex* > mVertices;
    std::list<ptr::shared_ptr<const TopicState> > mOutputList;
    // VANILLA_END
#endif
};


#if defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
// LITE_BEGIN
// TODO(DGRAPH-22): This is only needed on Lite and I'm not too sure about it yet.
template<class T>
Topic<T>::Topic(Graph* aGraph)
{
// Pre-C++11 type checking.
#if __cplusplus < 201103L
        (void)static_cast<TopicState*>((T*)0);
#endif
    aGraph->GetTopicRegistry().Register<T>(this);
}
// LITE_END
#endif

}

#endif // DETECTORGRAPH_INCLUDE_GRAPH_HPP_
