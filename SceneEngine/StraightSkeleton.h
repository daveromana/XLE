// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Vector.h"
#include "../Utility/IteratorUtils.h"

namespace SceneEngine
{
	namespace StraightSkeleton
	{
		class Skeleton
		{
		public:
			using VertexId = unsigned;
			using Edge = std::pair<VertexId, VertexId>;
			struct Face { std::vector<Edge> _edges; };

			std::vector<Float3> _steinerVertices;
			std::vector<Face> _faces;

			std::vector<Edge> _unplacedEdges;
		};

		class Vertex
		{
		public:
			Float2		_position;
			unsigned	_skeletonVertexId;
			float		_initialTime;
			Float2		_velocity;

			bool _noLongerActive;		// (collapsed out of the graph)
		};

		class Graph
		{
		public:
			std::vector<Vertex> _vertices;

			class Segment
			{
			public:
				unsigned	_head, _tail;
			};
			std::vector<Segment> _edgeSegments;

			class MotorcycleSegment
			{
			public:
				unsigned _head;
				unsigned _tail;		// (this is the fixed vertex)
			};
			std::vector<MotorcycleSegment> _motorcycleSegments;

			Skeleton GenerateSkeleton();
		};

		Graph BuildGraphFromVertexLoop(IteratorRange<const Float2*> vertices);
	}
}