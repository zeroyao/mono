#include <mono/metadata/object.h>
#include <mono/metadata/object-internals.h>
#include <mono/metadata/metadata.h>
#include <mono/metadata/tabledefs.h>
#include <mono/metadata/class-internals.h>
#include <mono/metadata/domain-internals.h>

//////////////////////////////////////////////////////////////////////////////

typedef void* (*MonoObjectGraphMalloc) (guint size);
typedef void (*MonoObjectGraphFree) (void* ptr);

typedef struct _LinearAllocator LinearAllocator;
typedef struct _MonoObjectGraph MonoObjectGraph;
typedef struct _ObjectGraphNode ObjectGraphNode;
typedef struct _ObjectGraphEdge ObjectGraphEdge;
typedef struct _ValueField ValueField;
typedef struct _QueuedNode QueuedNode;
typedef struct _TraverseContext TraverseContext;

#define LINEAR_ALLOCATOR_CHUNK_SIZE	(1024*10)

typedef struct _LinearAllocator
{
	guchar* buffer;
	guint totalSize;
	guint allocated;
	LinearAllocator* prev;
} LinearAllocator;

// graph's node, representing a reference i.e. MonoObject
struct _ObjectGraphNode
{
	MonoObject*			object;
	gboolean			isArray;
	ObjectGraphEdge*	edgesBegin;
	ObjectGraphEdge*	edgesEnd;
	ValueField*			valueFieldsBegin;
	ValueField*			valueFieldsEnd;
};

// graph's edge, representing a field of reference type
struct _ObjectGraphEdge
{
	ObjectGraphEdge*	next;
	const char*			name;
	ObjectGraphNode*	reference;
};

// a value field
struct _ValueField
{
	ValueField* next;
	const char*	name;
	MonoClass*	type;
};

struct _QueuedNode
{
	ObjectGraphNode* node;
	QueuedNode* next;
};

struct _TraverseContext
{
	MonoObjectGraph* g;
	QueuedNode* queueBegin;
	QueuedNode* queueEnd;
};

struct _MonoObjectGraph
{
	MonoObjectGraphMalloc	malloc;
	MonoObjectGraphFree		free;
	LinearAllocator*		allocator;

	MonoObject**			roots;
	QueuedNode*				allNodesBegin;
	QueuedNode*				allNodesEnd;
};

//////////////////////////////////////////////////////////////////////////////
// Memory management

void mono_object_graph_add_allocator (MonoObjectGraph* objectGraph)
{
	LinearAllocator* allocator = objectGraph->malloc (sizeof(LinearAllocator));
	allocator->buffer = objectGraph->malloc (LINEAR_ALLOCATOR_CHUNK_SIZE);
	allocator->totalSize = LINEAR_ALLOCATOR_CHUNK_SIZE;
	allocator->allocated = 0;
	allocator->prev = objectGraph->allocator;
	objectGraph->allocator = allocator;
}

void* mono_object_graph_allocate (MonoObjectGraph* g, guint size, guint alignment)
{
	gsize next = (gsize)g->allocator->buffer + g->allocator->allocated;
	next = (next + alignment - 1) & ~(alignment - 1);
	if (next + size > (gsize)g->allocator->buffer + g->allocator->totalSize)
	{
		mono_object_graph_add_allocator (g);
		next = (gsize)g->allocator->buffer;
		next = (next + alignment - 1) & ~(alignment - 1);
		if (next + size > (gsize)g->allocator->buffer + g->allocator->totalSize)
			return NULL;
	}
	g->allocator->allocated = next + size - (gsize)g->allocator->buffer;
	return (void*)next;
}

#define LINEAR_ALLOC(type, size, g) (type*)mono_object_graph_allocate(g, (size), 8)

MonoObjectGraph* mono_object_graph_initialize (MonoObjectGraphMalloc malloc, MonoObjectGraphFree free)
{
	MonoObjectGraph* g = malloc (sizeof(MonoObjectGraph));
	memset (g, 0, sizeof(MonoObjectGraph));
	g->malloc = malloc;
	g->free = free;
	mono_object_graph_add_allocator (g);
	return g;
}

void mono_object_graph_cleanup (MonoObjectGraph* g)
{
	LinearAllocator* alloc = g->allocator;
	while (alloc != NULL)
	{
		g->free (alloc->buffer);
		alloc = alloc->prev;
	}
	g->free (g);
}

//////////////////////////////////////////////////////////////////////////////

#define MARK_OBJ(obj) do { (obj)->vtable = (MonoVTable*)(((gsize)(obj)->vtable) | (gsize)1); } while (0)
#define IS_MARKED(obj) (((gsize)(obj)->vtable) & (gsize)1)

void mono_object_graph_enque (ObjectGraphNode* node, TraverseContext* ctx)
{
	QueuedNode* qnode;

	g_assert (node != NULL);
	if (IS_MARKED (node->object))
		return;

	qnode = LINEAR_ALLOC (QueuedNode, sizeof(QueuedNode), ctx->g);
	qnode->next = NULL;
	qnode->node = node;

	if (ctx->queueBegin == NULL)
	{
		g_assert (ctx->queueEnd == NULL);
		ctx->queueBegin = ctx->queueEnd = qnode;
	}
	else
	{
		ctx->queueEnd->next = qnode;
		ctx->queueEnd = qnode;
	}
}

ObjectGraphNode* mono_object_graph_deque (TraverseContext* ctx)
{
	QueuedNode* ret;

	if (ctx->queueBegin == NULL)
	{
		g_assert (ctx->queueEnd == NULL);
		return NULL;
	}

	ret = ctx->queueBegin;
	ctx->queueBegin = ret->next;
	if (ctx->queueBegin == NULL)
		ctx->queueEnd = NULL;

	return ret->node;
}

ObjectGraphNode* mono_object_graph_map (MonoObject* object, TraverseContext* ctx)
{
	QueuedNode* qnode;
	MonoObjectGraph* g = ctx->g;

	if (IS_MARKED (object))
	{
		for (qnode = g->allNodesBegin; qnode != NULL; qnode = qnode->next)
		{
			if (qnode->node->object == object)
				return qnode->node;
		}
		g_assert_not_reached ();
	}

	qnode = LINEAR_ALLOC (QueuedNode, sizeof(QueuedNode), g);
	qnode->next = NULL;
	qnode->node = LINEAR_ALLOC (ObjectGraphNode, sizeof(ObjectGraphNode), g);
	memset (qnode->node, 0, sizeof(ObjectGraphNode));
	qnode->node->object = object;
	MARK_OBJ (object);

	if (g->allNodesBegin == NULL)
	{
		g_assert (g->allNodesEnd == NULL);
		g->allNodesBegin = g->allNodesEnd = qnode;
	}
	else
	{
		g->allNodesEnd->next = qnode;
		g->allNodesEnd = qnode;
	}

	return qnode->node;
}

static void mono_object_graph_traverse_generic	(ObjectGraphNode* node, TraverseContext* ctx);
static void mono_object_graph_traverse_array	(ObjectGraphNode* node, TraverseContext* ctx);
static void mono_object_graph_traverse_object	(ObjectGraphNode* node, TraverseContext* ctx);

//////////////////////////////////////////////////////////////////////////////
// Public interface

MonoObjectGraph* mono_unity_object_graph_dump (MonoObjectGraphMalloc malloc, MonoObjectGraphFree free, gpointer* rootHandles, guint numRoots, guint memorySize)
{
	guint i;
	ObjectGraphNode* node;
	TraverseContext ctx;

	MonoObjectGraph* g = mono_object_graph_initialize(malloc, free);
	g->roots = LINEAR_ALLOC (MonoObject*, sizeof(MonoObject*)*numRoots, g);
	for (i = 0; i < numRoots; ++i)
		g->roots[i] = mono_gchandle_get_target (GPOINTER_TO_UINT (rootHandles[i]));

	// enqueue roots
	memset (&ctx, 0, sizeof(TraverseContext));
	ctx.g = g;
	for (i = 0; i < numRoots; ++i)
	{
		node = mono_object_graph_map (g->roots[i], &ctx);
		mono_object_graph_enque (node, &ctx);
	}

	while ((node = mono_object_graph_deque (&ctx)) != NULL)
	{
		mono_object_graph_traverse_generic (node, &ctx);
	}

	return g;
}

void mono_unity_object_graph_cleanup (MonoObjectGraph* objectGraph)
{
	mono_object_graph_cleanup (objectGraph);
}

//////////////////////////////////////////////////////////////////////////////
// Implementations

#define GET_VTABLE(obj) ((MonoVTable*)(((gsize)(obj)->vtable) & ~(gsize)1))

static void mono_object_graph_traverse_generic (ObjectGraphNode* node, TraverseContext* ctx)
{
	if (GET_VTABLE (node->object)->klass->rank)
		mono_object_graph_traverse_array (node, ctx);
	else
		mono_object_graph_traverse_object (node, ctx);
}

static void mono_object_graph_traverse_array (ObjectGraphNode* node, TraverseContext* ctx)
{
	MonoArray* array = (MonoArray*)node->object;
	MonoObject* object = node->object;

	MonoClass* elementClass;

	mono_array_size_t arrayLength, i;
	ObjectGraphEdge* edge;
	gint32 elementClassSize;
	MonoObject* elementObject;

	g_assert (object);
	elementClass = GET_VTABLE (object)->klass->element_class;
	g_assert (elementClass->size_inited != 0);

	node->isArray = TRUE;
	arrayLength = mono_array_length (array);

	for (i = 0; i < arrayLength; ++i)
	{
		edge = LINEAR_ALLOC (ObjectGraphEdge, sizeof(ObjectGraphEdge), ctx->g);
		memset (edge, 0, sizeof(ObjectGraphEdge));
		edge->name = (const char*)i;
		edge->next = NULL;

		if (node->edgesBegin == NULL)
		{
			g_assert (node->edgesEnd == NULL);
			node->edgesBegin = node->edgesEnd = edge;
		}
		else
		{
			node->edgesEnd->next = edge;
			node->edgesEnd = edge;
		}

		if (elementClass->valuetype)
		{
			elementClassSize = mono_class_array_element_size (elementClass);
			elementObject = (MonoObject*)mono_array_addr_with_size (array, elementClassSize, i);
		}
		else
		{
			elementObject = mono_array_get (array, MonoObject*, i);
		}

		edge->reference = mono_object_graph_map (elementObject, ctx);
		mono_object_graph_enque (edge->reference, ctx);
	}
}

void mono_object_graph_traverse_object (ObjectGraphNode* node, TraverseContext* ctx)
{
}
