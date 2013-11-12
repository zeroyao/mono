#include <mono/metadata/object.h>
#include <mono/metadata/object-internals.h>
#include <mono/metadata/metadata.h>
#include <mono/metadata/tabledefs.h>
#include <mono/metadata/class-internals.h>
#include <mono/metadata/domain-internals.h>
#include "unity_object_graph.h"

//////////////////////////////////////////////////////////////////////////////

typedef struct _TraverseContext TraverseContext;

struct _TraverseContext
{
	MonoObjectGraph* g;
	QueuedNode* queueBegin;
	QueuedNode* queueEnd;
};

//////////////////////////////////////////////////////////////////////////////
// Memory management

void mono_object_graph_initialize_allocator (MonoObjectGraph* objectGraph, guchar* buffer, guint32 bufferSize)
{
	LinearAllocator* allocator = &objectGraph->allocator;
	allocator->buffer = buffer;
	allocator->totalSize = bufferSize;
	allocator->allocated = 0;
}

void* mono_object_graph_allocate (MonoObjectGraph* g, guint32 size, guint32 alignment)
{
	gsize next = (gsize)g->allocator.buffer + g->allocator.allocated;
	next = (next + alignment - 1) & ~(alignment - 1);
	if (next + size > (gsize)g->allocator.buffer + g->allocator.totalSize)
	{
		g_assert_not_reached ();
		return NULL;
	}
	g->allocator.allocated = next + size - (gsize)g->allocator.buffer;
	return (void*)next;
}

#define LINEAR_ALLOC(type, size, g) (type*)mono_object_graph_allocate(g, (size), 8)

MonoObjectGraph* mono_object_graph_initialize (gpointer buffer, guint32 bufferSize)
{
	MonoObjectGraph* g = (MonoObjectGraph*)(((gsize)buffer + 7) & ~7);
	g_assert (((gsize)(g + 1) - (gsize)buffer) <= bufferSize);

	memset (g, 0, sizeof(MonoObjectGraph));
	mono_object_graph_initialize_allocator (g, (guchar*)(g + 1), (gsize)buffer + bufferSize - (gsize)(g + 1));
	return g;
}

void mono_object_graph_cleanup (MonoObjectGraph* g)
{
}

//////////////////////////////////////////////////////////////////////////////

#define MARK_OBJ(obj) do { (obj)->vtable = (MonoVTable*)(((gsize)(obj)->vtable) | (gsize)1); } while (0)
#define IS_MARKED(obj) (((gsize)(obj)->vtable) & (gsize)1)
#define GET_VTABLE(obj) ((MonoVTable*)(((gsize)(obj)->vtable) & ~(gsize)1))

void mono_object_graph_enque (ObjectGraphNode* node, TraverseContext* ctx)
{
	QueuedNode* qnode;

	g_assert (node != NULL);

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

ObjectGraphNode* mono_object_graph_map (MonoObject* object, gboolean hasVTable, MonoClass* klass, TraverseContext* ctx)
{
	QueuedNode* qnode;
	MonoObjectGraph* g = ctx->g;

	g_assert (object != NULL && klass != NULL);

	if (hasVTable && IS_MARKED (object))
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
	qnode->node->klass = klass;

	if (!hasVTable || !IS_MARKED(object))
		mono_object_graph_enque (qnode->node, ctx);

	if (hasVTable)
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

//////////////////////////////////////////////////////////////////////////////

extern void GC_stop_world_external ();
extern void GC_start_world_external ();
static void mono_object_graph_traverse_generic	(ObjectGraphNode* node, TraverseContext* ctx);
static void mono_object_graph_traverse_array (ObjectGraphNode* node, TraverseContext* ctx);
static void mono_object_graph_traverse_object (ObjectGraphNode* node, TraverseContext* ctx);
static void mono_object_graph_push_edge (ObjectGraphNode* node, const char* name, EdgeType edgeType, ObjectGraphNode* reference, TraverseContext* ctx);

//////////////////////////////////////////////////////////////////////////////
// Public interface

MonoObjectGraph* mono_unity_object_graph_dump (MonoObject** rootObjects, guint32 numRoots, gpointer buffer, guint32 bufferSize)
{
	guint32 i;
	ObjectGraphNode* node;
	TraverseContext ctx;
	MonoObjectGraph* g;

	GC_stop_world_external ();

	g = mono_object_graph_initialize(buffer, bufferSize);
	g->roots = LINEAR_ALLOC (MonoObject*, sizeof(MonoObject*) * numRoots, g);
	memcpy (g->roots, rootObjects, sizeof(MonoObject*) * numRoots);
	g->rootNodes = LINEAR_ALLOC (ObjectGraphNode*, sizeof(ObjectGraphNode*) * numRoots, g);
	memset (g->rootNodes, 0, sizeof(ObjectGraphNode*) * numRoots);

	// enqueue roots
	memset (&ctx, 0, sizeof(TraverseContext));
	ctx.g = g;
	for (i = 0; i < numRoots; ++i)
		g->rootNodes[i] = mono_object_graph_map (g->roots[i], TRUE, GET_VTABLE (g->roots[i])->klass, &ctx);

	while ((node = mono_object_graph_deque (&ctx)) != NULL)
	{
		mono_object_graph_traverse_generic (node, &ctx);
	}

	GC_start_world_external ();

	return g;
}

void mono_unity_object_graph_cleanup (MonoObjectGraph* objectGraph)
{
	mono_object_graph_cleanup (objectGraph);
}

//////////////////////////////////////////////////////////////////////////////
// Implementations

static void mono_object_graph_traverse_generic (ObjectGraphNode* node, TraverseContext* ctx)
{
	if (node->klass->rank)
		mono_object_graph_traverse_array (node, ctx);
	else
		mono_object_graph_traverse_object (node, ctx);
}

static void mono_object_graph_traverse_array (ObjectGraphNode* node, TraverseContext* ctx)
{
	MonoArray* array = (MonoArray*)node->object;
	MonoObject* object = node->object;
	MonoClass* klass = node->klass;

	MonoClass* elementClass;
	gboolean isElementValueType;

	mono_array_size_t arrayLength, i;
	gint32 elementClassSize;
	MonoObject* elementObject;
	ObjectGraphNode* elementNode;

	g_assert (object);
	elementClass = klass->element_class;
	isElementValueType = elementClass->valuetype;
	g_assert (elementClass->size_inited != 0);

	arrayLength = mono_array_length (array);

	for (i = 0; i < arrayLength; ++i)
	{
		if (isElementValueType)
		{
			elementClassSize = mono_class_array_element_size (elementClass);
			elementObject = (MonoObject*)mono_array_addr_with_size (array, elementClassSize, i);
			// subtract the added offset for the vtable. This is added to the offset even though it is a struct
			--elementObject;
		}
		else
		{
			elementObject = mono_array_get (array, MonoObject*, i);
		}

		elementNode = elementObject != NULL ? mono_object_graph_map (elementObject, !isElementValueType, elementClass, ctx) : NULL;
		mono_object_graph_push_edge (node, (const char*)i, EdgeType_ArrayElement, elementNode, ctx);
	}
}

static void mono_object_graph_traverse_object (ObjectGraphNode* node, TraverseContext* ctx)
{
	MonoObject* object = node->object;
	MonoClass* klass = node->klass;

	guint32 i;
	MonoClassField* field;
	EdgeType edgeType;
	ObjectGraphNode* edgeNode;

	g_assert (object);

	for (; klass != NULL; klass = klass->parent)
	{
		if (klass->size_inited == 0)
			continue;

		for (i = 0; i < klass->field.count; ++i)
		{
			field = &klass->fields[i];
			if (field->type->attrs & FIELD_ATTRIBUTE_STATIC)
				continue;

			edgeType = EdgeType_Value;
			edgeNode = NULL;

			if (MONO_TYPE_ISSTRUCT (field->type))
			{
				gchar* offseted = (gchar*)object;
				offseted += field->offset;
				if (field->type->type == MONO_TYPE_GENERICINST)
				{
					g_assert (field->type->data.generic_class->cached_class);
					edgeNode = mono_object_graph_map ((MonoObject*)offseted - 1, FALSE, field->type->data.generic_class->cached_class, ctx);
				}
				else
					edgeNode = mono_object_graph_map ((MonoObject*)offseted - 1, FALSE, field->type->data.klass, ctx);
			}
			else
			{
				g_assert (field->offset != -1);

				if (MONO_TYPE_IS_REFERENCE(field->type))
				{
					MonoObject* val = NULL;
					mono_field_get_value (object, field, &val);
					if (val != NULL)
						edgeNode = mono_object_graph_map (val, TRUE, GET_VTABLE (val)->klass, ctx);
					edgeType = EdgeType_Reference;
				}
			}

			mono_object_graph_push_edge (node, field->name, edgeType, edgeNode, ctx);
		}
	}
}

static void mono_object_graph_push_edge (ObjectGraphNode* node, const char* name, EdgeType type, ObjectGraphNode* otherNode, TraverseContext* ctx)
{
	ObjectGraphEdge* edge;

	g_assert (node != NULL);

	edge = LINEAR_ALLOC (ObjectGraphEdge, sizeof(ObjectGraphEdge), ctx->g);
	memset (edge, 0, sizeof(ObjectGraphEdge));
	edge->name = name;
	edge->type = type;
	edge->otherNode = otherNode;

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
}
