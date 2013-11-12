#ifndef _MONO_UNITY_OBJECT_GRAPH_H_
#define _MONO_UNITY_OBJECT_GRAPH_H_

typedef struct _LinearAllocator LinearAllocator;
typedef struct _MonoObjectGraph MonoObjectGraph;
typedef struct _ObjectGraphNode ObjectGraphNode;
typedef struct _ObjectGraphEdge ObjectGraphEdge;
//typedef struct _ValueField ValueField;
typedef struct _QueuedNode QueuedNode;
typedef enum _EdgeType EdgeType;

typedef struct _LinearAllocator
{
	guchar* buffer;
	guint32 totalSize;
	guint32 allocated;
} LinearAllocator;

// graph's node, representing a reference i.e. MonoObject
struct _ObjectGraphNode
{
	MonoObject*			object;
	MonoClass*			klass;
	ObjectGraphEdge*	edgesBegin;
	ObjectGraphEdge*	edgesEnd;
	//ValueField*			valueFieldsBegin;
	//ValueField*			valueFieldsEnd;
};

// graph's edge, representing a field of reference type
struct _ObjectGraphEdge
{
	ObjectGraphEdge*	next;
	const char*			name;
	EdgeType			type;
	ObjectGraphNode*	otherNode;
};

enum _EdgeType
{
	EdgeType_Reference,
	EdgeType_Value,
	EdgeType_ArrayElement,
};

//// a value field
//struct _ValueField
//{
//	ValueField* next;
//	const char*	name;
//	MonoClass*	type;
//};

struct _QueuedNode
{
	ObjectGraphNode* node;
	QueuedNode* next;
};

struct _MonoObjectGraph
{
	LinearAllocator	allocator;

	MonoObject**	roots;
	QueuedNode*		allNodesBegin;
	QueuedNode*		allNodesEnd;
};

#endif
