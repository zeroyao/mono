#ifndef _MONO_UNITY_OBJECT_GRAPH_H_
#define _MONO_UNITY_OBJECT_GRAPH_H_

typedef struct _LinearAllocator LinearAllocator;
typedef struct _MonoObjectGraph MonoObjectGraph;
typedef struct _ObjectGraphNode ObjectGraphNode;
typedef struct _ObjectGraphEdge ObjectGraphEdge;
typedef struct _ValueField ValueField;
typedef struct _QueuedNode QueuedNode;

typedef struct _LinearAllocator
{
	guchar* buffer;
	guint totalSize;
	guint allocated;
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
	ObjectGraphNode*	reference;
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
