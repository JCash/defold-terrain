#ifndef DM_TERRAIN_BUFFER_H
#define DM_TERRAIN_BUFFER_H

#include <dmsdk/dlib/buffer.h>
#include <dmsdk/resource/resource.h>
#include <dmsdk/gamesys/resources/res_buffer.h>
#include <gamesys/buffer_ddf.h>

ResourceResult CreateBufferResource(HResourceFactory factory, const char* path,
                                   const dmBuffer::StreamDeclaration* streams_decl,
                                   uint32_t streams_decl_count,
                                   uint32_t num_vertices,
                                   dmGameSystem::BufferResource** out_resource);


#endif // DM_TERRAIN_BUFFER_H
