#ifndef _MONO_UNITY_OBJECT_GRAPH_H_
#define _MONO_UNITY_OBJECT_GRAPH_H_

typedef struct _LinearAllocator LinearAllocator;
typedef struct _MonoObjectGraph MonoObjectGraph;
typedef struct _ObjectGraphNode ObjectGraphNode;
typedef struct _ObjectGraphField ObjectGraphField;
typedef struct _QueuedNode QueuedNode;

typedef struct _LinearAllocator
{
	guchar* buffer;
	guint32 totalSize;
	guint32 allocated;
} LinearAllocator;

// graph's node, representing a reference i.e. MonoObject
struct _ObjectGraphNode
{
	guint32				index;
	MonoObject*			object;
	MonoClass*			klass;
	guint32				classType;
	const char*			className;
	guint32				classSize;
	ObjectGraphField*	fieldsBegin;
	ObjectGraphField*	fieldsEnd;
	ObjectGraphField*	staticFieldsBegin;
	ObjectGraphField*	staticFieldsEnd;
};

// graph's edge, representing a field
struct _ObjectGraphField
{
	ObjectGraphField*	next;
	const char*			name;
	ObjectGraphNode*	otherNode;
};

struct _QueuedNode
{
	ObjectGraphNode* node;
	QueuedNode* next;
};

struct _MonoObjectGraph
{
	LinearAllocator		allocator;

	MonoObject**		roots;
	ObjectGraphNode**	rootNodes;
	guint32				numRoots;

	QueuedNode*			allNodesBegin;
	QueuedNode*			allNodesEnd;
	guint32				numAllNodes;
};

enum
{
	ClassType_Reference	= 0,
	ClassType_Value		= 1,
	ClassType_Array		= 0x80000000,
};

#endif
