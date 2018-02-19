/**************************************************************************
 * 
 * Copyright 2007 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

#ifndef U_INLINES_H
#define U_INLINES_H

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_shader_tokens.h"
#include "pipe/p_state.h"
#include "pipe/p_screen.h"
#include "util/u_debug.h"
#include "util/u_debug_describe.h"
#include "util/u_debug_refcnt.h"
#include "util/u_atomic.h"
#include "util/u_box.h"
#include "util/u_math.h"


#ifdef __cplusplus
extern "C" {
#endif


/*
 * Reference counting helper functions.
 */


static inline void
pipe_reference_init(struct pipe_reference *reference, unsigned count)
{
   p_atomic_set(&reference->count, count);
}

static inline boolean
pipe_is_referenced(struct pipe_reference *reference)
{
   return p_atomic_read(&reference->count) != 0;
}

/**
 * Update reference counting.
 * The old thing pointed to, if any, will be unreferenced.
 * Both 'ptr' and 'reference' may be NULL.
 * \return TRUE if the object's refcount hits zero and should be destroyed.
 */
static inline boolean
pipe_reference_described(struct pipe_reference *ptr, 
                         struct pipe_reference *reference, 
                         debug_reference_descriptor get_desc)
{
   boolean destroy = FALSE;

   if(ptr != reference) {
      /* bump the reference.count first */
      if (reference) {
         assert(pipe_is_referenced(reference));
         p_atomic_inc(&reference->count);
         debug_reference(reference, get_desc, 1);
      }

      if (ptr) {
         assert(pipe_is_referenced(ptr));
         if (p_atomic_dec_zero(&ptr->count)) {
            destroy = TRUE;
         }
         debug_reference(ptr, get_desc, -1);
      }
   }

   return destroy;
}

static inline boolean
pipe_reference(struct pipe_reference *ptr, struct pipe_reference *reference)
{
   return pipe_reference_described(ptr, reference, 
                                   (debug_reference_descriptor)debug_describe_reference);
}

static inline void
pipe_surface_reference(struct pipe_surface **ptr, struct pipe_surface *surf)
{
   struct pipe_surface *old_surf = *ptr;

   if (pipe_reference_described(&(*ptr)->reference, &surf->reference, 
                                (debug_reference_descriptor)debug_describe_surface))
      old_surf->context->surface_destroy(old_surf->context, old_surf);
   *ptr = surf;
}

/**
 * Similar to pipe_surface_reference() but always set the pointer to NULL
 * and pass in an explicit context.  The explicit context avoids the problem
 * of using a deleted context's surface_destroy() method when freeing a surface
 * that's shared by multiple contexts.
 */
static inline void
pipe_surface_release(struct pipe_context *pipe, struct pipe_surface **ptr)
{
   if (pipe_reference_described(&(*ptr)->reference, NULL,
                                (debug_reference_descriptor)debug_describe_surface))
      pipe->surface_destroy(pipe, *ptr);
   *ptr = NULL;
}


static inline void
pipe_resource_reference(struct pipe_resource **ptr, struct pipe_resource *tex)
{
   struct pipe_resource *old_tex = *ptr;

   if (pipe_reference_described(&(*ptr)->reference, &tex->reference, 
                                (debug_reference_descriptor)debug_describe_resource)) {
      /* Avoid recursion, which would prevent inlining this function */
      do {
         struct pipe_resource *next = old_tex->next;

         old_tex->screen->resource_destroy(old_tex->screen, old_tex);
         old_tex = next;
      } while (pipe_reference_described(&old_tex->reference, NULL,
                                        (debug_reference_descriptor)debug_describe_resource));
   }
   *ptr = tex;
}

/**
 * Set *ptr to \p view with proper reference counting.
 *
 * The caller must guarantee that \p view and *ptr must have been created in
 * the same context (if they exist), and that this must be the current context.
 */
static inline void
pipe_sampler_view_reference(struct pipe_sampler_view **ptr, struct pipe_sampler_view *view)
{
   struct pipe_sampler_view *old_view = *ptr;

   if (pipe_reference_described(&(*ptr)->reference, &view->reference,
                                (debug_reference_descriptor)debug_describe_sampler_view))
      old_view->context->sampler_view_destroy(old_view->context, old_view);
   *ptr = view;
}

/**
 * Similar to pipe_sampler_view_reference() but always set the pointer to
 * NULL and pass in the current context explicitly.
 *
 * If *ptr is non-NULL, it may refer to a view that was created in a different
 * context (however, that context must still be alive).
 */
static inline void
pipe_sampler_view_release(struct pipe_context *ctx,
                          struct pipe_sampler_view **ptr)
{
   struct pipe_sampler_view *old_view = *ptr;
   if (pipe_reference_described(&(*ptr)->reference, NULL,
                    (debug_reference_descriptor)debug_describe_sampler_view)) {
      ctx->sampler_view_destroy(ctx, old_view);
   }
   *ptr = NULL;
}

static inline void
pipe_so_target_reference(struct pipe_stream_output_target **ptr,
                         struct pipe_stream_output_target *target)
{
   struct pipe_stream_output_target *old = *ptr;

   if (pipe_reference_described(&(*ptr)->reference, &target->reference,
                     (debug_reference_descriptor)debug_describe_so_target))
      old->context->stream_output_target_destroy(old->context, old);
   *ptr = target;
}

static inline void
pipe_vertex_buffer_unreference(struct pipe_vertex_buffer *dst)
{
   if (dst->is_user_buffer)
      dst->buffer.user = NULL;
   else
      pipe_resource_reference(&dst->buffer.resource, NULL);
}

static inline void
pipe_vertex_buffer_reference(struct pipe_vertex_buffer *dst,
                             const struct pipe_vertex_buffer *src)
{
   pipe_vertex_buffer_unreference(dst);
   if (!src->is_user_buffer)
      pipe_resource_reference(&dst->buffer.resource, src->buffer.resource);
   memcpy(dst, src, sizeof(*src));
}

static inline void
pipe_surface_reset(struct pipe_context *ctx, struct pipe_surface* ps,
                   struct pipe_resource *pt, unsigned level, unsigned layer)
{
   pipe_resource_reference(&ps->texture, pt);
   ps->format = pt->format;
   ps->width = u_minify(pt->width0, level);
   ps->height = u_minify(pt->height0, level);
   ps->u.tex.level = level;
   ps->u.tex.first_layer = ps->u.tex.last_layer = layer;
   ps->context = ctx;
}

static inline void
pipe_surface_init(struct pipe_context *ctx, struct pipe_surface* ps,
                  struct pipe_resource *pt, unsigned level, unsigned layer)
{
   ps->texture = 0;
   pipe_reference_init(&ps->reference, 1);
   pipe_surface_reset(ctx, ps, pt, level, layer);
}

/* Return true if the surfaces are equal. */
static inline boolean
pipe_surface_equal(struct pipe_surface *s1, struct pipe_surface *s2)
{
   return s1->texture == s2->texture &&
          s1->format == s2->format &&
          (s1->texture->target != PIPE_BUFFER ||
           (s1->u.buf.first_element == s2->u.buf.first_element &&
            s1->u.buf.last_element == s2->u.buf.last_element)) &&
          (s1->texture->target == PIPE_BUFFER ||
           (s1->u.tex.level == s2->u.tex.level &&
            s1->u.tex.first_layer == s2->u.tex.first_layer &&
            s1->u.tex.last_layer == s2->u.tex.last_layer));
}

/*
 * Convenience wrappers for screen buffer functions.
 */


/**
 * Create a new resource.
 * \param bind  bitmask of PIPE_BIND_x flags
 * \param usage  a PIPE_USAGE_x value
 */
static inline struct pipe_resource *
pipe_buffer_create( struct pipe_screen *screen,
		    unsigned bind,
		    enum pipe_resource_usage usage,
		    unsigned size )
{
   struct pipe_resource buffer;
   memset(&buffer, 0, sizeof buffer);
   buffer.target = PIPE_BUFFER;
   buffer.format = PIPE_FORMAT_R8_UNORM; /* want TYPELESS or similar */
   buffer.bind = bind;
   buffer.usage = usage;
   buffer.flags = 0;
   buffer.width0 = size;
   buffer.height0 = 1;
   buffer.depth0 = 1;
   buffer.array_size = 1;
   return screen->resource_create(screen, &buffer);
}


static inline struct pipe_resource *
pipe_buffer_create_const0(struct pipe_screen *screen,
                          unsigned bind,
                          enum pipe_resource_usage usage,
                          unsigned size)
{
   struct pipe_resource buffer;
   memset(&buffer, 0, sizeof buffer);
   buffer.target = PIPE_BUFFER;
   buffer.format = PIPE_FORMAT_R8_UNORM;
   buffer.bind = bind;
   buffer.usage = usage;
   buffer.flags = screen->get_param(screen, PIPE_CAP_CONSTBUF0_FLAGS);
   buffer.width0 = size;
   buffer.height0 = 1;
   buffer.depth0 = 1;
   buffer.array_size = 1;
   return screen->resource_create(screen, &buffer);
}


/**
 * Map a range of a resource.
 * \param offset  start of region, in bytes 
 * \param length  size of region, in bytes 
 * \param access  bitmask of PIPE_TRANSFER_x flags
 * \param transfer  returns a transfer object
 */
static inline void *
pipe_buffer_map_range(struct pipe_context *pipe,
		      struct pipe_resource *buffer,
		      unsigned offset,
		      unsigned length,
		      unsigned access,
		      struct pipe_transfer **transfer)
{
   struct pipe_box box;
   void *map;

   assert(offset < buffer->width0);
   assert(offset + length <= buffer->width0);
   assert(length);

   u_box_1d(offset, length, &box);

   map = pipe->transfer_map(pipe, buffer, 0, access, &box, transfer);
   if (!map) {
      return NULL;
   }

   return map;
}


/**
 * Map whole resource.
 * \param access  bitmask of PIPE_TRANSFER_x flags
 * \param transfer  returns a transfer object
 */
static inline void *
pipe_buffer_map(struct pipe_context *pipe,
                struct pipe_resource *buffer,
                unsigned access,
                struct pipe_transfer **transfer)
{
   return pipe_buffer_map_range(pipe, buffer, 0, buffer->width0, access, transfer);
}


static inline void
pipe_buffer_unmap(struct pipe_context *pipe,
                  struct pipe_transfer *transfer)
{
   pipe->transfer_unmap(pipe, transfer);
}

static inline void
pipe_buffer_flush_mapped_range(struct pipe_context *pipe,
                               struct pipe_transfer *transfer,
                               unsigned offset,
                               unsigned length)
{
   struct pipe_box box;
   int transfer_offset;

   assert(length);
   assert(transfer->box.x <= (int) offset);
   assert((int) (offset + length) <= transfer->box.x + transfer->box.width);

   /* Match old screen->buffer_flush_mapped_range() behaviour, where
    * offset parameter is relative to the start of the buffer, not the
    * mapped range.
    */
   transfer_offset = offset - transfer->box.x;

   u_box_1d(transfer_offset, length, &box);

   pipe->transfer_flush_region(pipe, transfer, &box);
}

static inline void
pipe_buffer_write(struct pipe_context *pipe,
                  struct pipe_resource *buf,
                  unsigned offset,
                  unsigned size,
                  const void *data)
{
   /* Don't set any other usage bits. Drivers should derive them. */
   pipe->buffer_subdata(pipe, buf, PIPE_TRANSFER_WRITE, offset, size, data);
}

/**
 * Special case for writing non-overlapping ranges.
 *
 * We can avoid GPU/CPU synchronization when writing range that has never
 * been written before.
 */
static inline void
pipe_buffer_write_nooverlap(struct pipe_context *pipe,
                            struct pipe_resource *buf,
                            unsigned offset, unsigned size,
                            const void *data)
{
   pipe->buffer_subdata(pipe, buf,
                        (PIPE_TRANSFER_WRITE |
                         PIPE_TRANSFER_UNSYNCHRONIZED),
                        offset, size, data);
}


/**
 * Create a new resource and immediately put data into it
 * \param bind  bitmask of PIPE_BIND_x flags
 * \param usage  bitmask of PIPE_USAGE_x flags
 */
static inline struct pipe_resource *
pipe_buffer_create_with_data(struct pipe_context *pipe,
                             unsigned bind,
                             enum pipe_resource_usage usage,
                             unsigned size,
                             const void *ptr)
{
   struct pipe_resource *res = pipe_buffer_create(pipe->screen,
                                                  bind, usage, size);
   pipe_buffer_write_nooverlap(pipe, res, 0, size, ptr);
   return res;
}

static inline void
pipe_buffer_read(struct pipe_context *pipe,
                 struct pipe_resource *buf,
                 unsigned offset,
                 unsigned size,
                 void *data)
{
   struct pipe_transfer *src_transfer;
   ubyte *map;

   map = (ubyte *) pipe_buffer_map_range(pipe,
					 buf,
					 offset, size,
					 PIPE_TRANSFER_READ,
					 &src_transfer);
   if (!map)
      return;

   memcpy(data, map, size);
   pipe_buffer_unmap(pipe, src_transfer);
}


/**
 * Map a resource for reading/writing.
 * \param access  bitmask of PIPE_TRANSFER_x flags
 */
static inline void *
pipe_transfer_map(struct pipe_context *context,
                  struct pipe_resource *resource,
                  unsigned level, unsigned layer,
                  unsigned access,
                  unsigned x, unsigned y,
                  unsigned w, unsigned h,
                  struct pipe_transfer **transfer)
{
   struct pipe_box box;
   u_box_2d_zslice(x, y, layer, w, h, &box);
   return context->transfer_map(context,
                                resource,
                                level,
                                access,
                                &box, transfer);
}


/**
 * Map a 3D (texture) resource for reading/writing.
 * \param access  bitmask of PIPE_TRANSFER_x flags
 */
static inline void *
pipe_transfer_map_3d(struct pipe_context *context,
                     struct pipe_resource *resource,
                     unsigned level,
                     unsigned access,
                     unsigned x, unsigned y, unsigned z,
                     unsigned w, unsigned h, unsigned d,
                     struct pipe_transfer **transfer)
{
   struct pipe_box box;
   u_box_3d(x, y, z, w, h, d, &box);
   return context->transfer_map(context,
                                resource,
                                level,
                                access,
                                &box, transfer);
}

static inline void
pipe_transfer_unmap( struct pipe_context *context,
                     struct pipe_transfer *transfer )
{
   context->transfer_unmap( context, transfer );
}

static inline void
pipe_set_constant_buffer(struct pipe_context *pipe,
                         enum pipe_shader_type shader, uint index,
                         struct pipe_resource *buf)
{
   if (buf) {
      struct pipe_constant_buffer cb;
      cb.buffer = buf;
      cb.buffer_offset = 0;
      cb.buffer_size = buf->width0;
      cb.user_buffer = NULL;
      pipe->set_constant_buffer(pipe, shader, index, &cb);
   } else {
      pipe->set_constant_buffer(pipe, shader, index, NULL);
   }
}


/**
 * Get the polygon offset enable/disable flag for the given polygon fill mode.
 * \param fill_mode  one of PIPE_POLYGON_MODE_POINT/LINE/FILL
 */
static inline boolean
util_get_offset(const struct pipe_rasterizer_state *templ,
                unsigned fill_mode)
{
   switch(fill_mode) {
   case PIPE_POLYGON_MODE_POINT:
      return templ->offset_point;
   case PIPE_POLYGON_MODE_LINE:
      return templ->offset_line;
   case PIPE_POLYGON_MODE_FILL:
      return templ->offset_tri;
   default:
      assert(0);
      return FALSE;
   }
}

static inline float
util_get_min_point_size(const struct pipe_rasterizer_state *state)
{
   /* The point size should be clamped to this value at the rasterizer stage.
    */
   return !state->point_quad_rasterization &&
          !state->point_smooth &&
          !state->multisample ? 1.0f : 0.0f;
}

static inline void
util_query_clear_result(union pipe_query_result *result, unsigned type)
{
   switch (type) {
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
   case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
   case PIPE_QUERY_GPU_FINISHED:
      result->b = FALSE;
      break;
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIME_ELAPSED:
   case PIPE_QUERY_PRIMITIVES_GENERATED:
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      result->u64 = 0;
      break;
   case PIPE_QUERY_SO_STATISTICS:
      memset(&result->so_statistics, 0, sizeof(result->so_statistics));
      break;
   case PIPE_QUERY_TIMESTAMP_DISJOINT:
      memset(&result->timestamp_disjoint, 0, sizeof(result->timestamp_disjoint));
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS:
      memset(&result->pipeline_statistics, 0, sizeof(result->pipeline_statistics));
      break;
   default:
      memset(result, 0, sizeof(*result));
   }
}

/** Convert PIPE_TEXTURE_x to TGSI_TEXTURE_x */
static inline enum tgsi_texture_type
util_pipe_tex_to_tgsi_tex(enum pipe_texture_target pipe_tex_target,
                          unsigned nr_samples)
{
   switch (pipe_tex_target) {
   case PIPE_BUFFER:
      return TGSI_TEXTURE_BUFFER;

   case PIPE_TEXTURE_1D:
      assert(nr_samples <= 1);
      return TGSI_TEXTURE_1D;

   case PIPE_TEXTURE_2D:
      return nr_samples > 1 ? TGSI_TEXTURE_2D_MSAA : TGSI_TEXTURE_2D;

   case PIPE_TEXTURE_RECT:
      assert(nr_samples <= 1);
      return TGSI_TEXTURE_RECT;

   case PIPE_TEXTURE_3D:
      assert(nr_samples <= 1);
      return TGSI_TEXTURE_3D;

   case PIPE_TEXTURE_CUBE:
      assert(nr_samples <= 1);
      return TGSI_TEXTURE_CUBE;

   case PIPE_TEXTURE_1D_ARRAY:
      assert(nr_samples <= 1);
      return TGSI_TEXTURE_1D_ARRAY;

   case PIPE_TEXTURE_2D_ARRAY:
      return nr_samples > 1 ? TGSI_TEXTURE_2D_ARRAY_MSAA :
                              TGSI_TEXTURE_2D_ARRAY;

   case PIPE_TEXTURE_CUBE_ARRAY:
      return TGSI_TEXTURE_CUBE_ARRAY;

   default:
      assert(0 && "unexpected texture target");
      return TGSI_TEXTURE_UNKNOWN;
   }
}


static inline void
util_copy_constant_buffer(struct pipe_constant_buffer *dst,
                          const struct pipe_constant_buffer *src)
{
   if (src) {
      pipe_resource_reference(&dst->buffer, src->buffer);
      dst->buffer_offset = src->buffer_offset;
      dst->buffer_size = src->buffer_size;
      dst->user_buffer = src->user_buffer;
   }
   else {
      pipe_resource_reference(&dst->buffer, NULL);
      dst->buffer_offset = 0;
      dst->buffer_size = 0;
      dst->user_buffer = NULL;
   }
}

static inline void
util_copy_image_view(struct pipe_image_view *dst,
                     const struct pipe_image_view *src)
{
   if (src) {
      pipe_resource_reference(&dst->resource, src->resource);
      dst->format = src->format;
      dst->access = src->access;
      dst->u = src->u;
   } else {
      pipe_resource_reference(&dst->resource, NULL);
      dst->format = PIPE_FORMAT_NONE;
      dst->access = 0;
      memset(&dst->u, 0, sizeof(dst->u));
   }
}

static inline unsigned
util_max_layer(const struct pipe_resource *r, unsigned level)
{
   switch (r->target) {
   case PIPE_TEXTURE_3D:
      return u_minify(r->depth0, level) - 1;
   case PIPE_TEXTURE_CUBE:
      assert(r->array_size == 6);
      /* fall-through */
   case PIPE_TEXTURE_1D_ARRAY:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_CUBE_ARRAY:
      return r->array_size - 1;
   default:
      return 0;
   }
}

static inline unsigned
util_num_layers(const struct pipe_resource *r, unsigned level)
{
   return util_max_layer(r, level) + 1;
}

static inline bool
util_texrange_covers_whole_level(const struct pipe_resource *tex,
                                 unsigned level, unsigned x, unsigned y,
                                 unsigned z, unsigned width,
                                 unsigned height, unsigned depth)
{
   return x == 0 && y == 0 && z == 0 &&
          width == u_minify(tex->width0, level) &&
          height == u_minify(tex->height0, level) &&
          depth == util_num_layers(tex, level);
}

#ifdef __cplusplus
}
#endif

#endif /* U_INLINES_H */
