#pragma once

#include "texture_upload.h"

struct MovieMetalUploadState {
    void* texture_cache = nullptr;
    void* pipeline = nullptr;
    void* sampler = nullptr;
    uint64_t pipeline_format = 0;
};

void movie_metal_upload_release(MovieMetalUploadState& state);

bool movie_upload_cv_pixel_buffer_metal(WGPUDevice device,
                                        WGPUQueue queue,
                                        const MovieTextureState& dst,
                                        void* pixel_buffer,
                                        MovieMetalUploadState& state,
                                        float* elapsed_us,
                                        bool* import_failed);
