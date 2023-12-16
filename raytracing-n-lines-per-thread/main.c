/**
 * Copyright (c) 2020, Evgeniy Morozov
 * All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stdio.h>
#include <pthread.h>
#include "hittables/rt_hittable_list.h"
#include "rt_camera.h"
#include "rt_skybox_simple.h"
#include <errno.h>
#include <string.h>
#include <scenes/rt_scenes.h>
#include <assert.h>

#define NUM_THREADS 8
#define NUMBER_OF_MACHINE_BYTES 64


typedef struct
{
    int tid;
    int begin;
    int end;
} thread_n_lines_of_work;

typedef struct
{
    int size;
    colour_t **pixel_matrix;
} thread_work_return;

int GLOBAL_IMAGE_WIDTH;
int GLOBAL_IMAGE_HEIGHT;
int GLOBAL_NUMBER_OF_SAMPLES;
rt_camera_t *GLOBAL_CAMERA;
rt_hittable_list_t *GLOBAL_WORLD;
rt_skybox_t *GLOBAL_SKYBOX;
int GLOBAL_CHILD_RAYS;

static void show_usage(const char *program_name, int err);

static colour_t ray_colour(const ray_t *ray, const rt_hittable_list_t *list, rt_skybox_t *skybox, int child_rays)
{
    if (child_rays <= 0)
    {
        return colour(0, 0, 0);
    }

    rt_hit_record_t record;
    if (!rt_hittable_list_hit_test(list, ray, 0.001, INFINITY, &record))
    {
        return rt_skybox_value(skybox, ray);
    }

    ray_t scattered;
    colour_t attenuation;
    colour_t emitted = rt_material_emit(record.material, record.u, record.v, &record.p);
    if (rt_material_scatter(record.material, ray, &record, &attenuation, &scattered))
    {
        return vec3_sum(emitted, vec3_multiply(attenuation, ray_colour(&scattered, list, skybox, child_rays - 1)));
    }
    return emitted;
}

void *process_n_lines_per_thread(void *args)
{

    thread_n_lines_of_work *thread = (thread_n_lines_of_work *)args;
    thread_work_return *thread_return = (thread_work_return *)malloc(sizeof(thread_work_return) * 4);    
    
    int real_size;
    thread_return->size = thread->begin - thread->end + 1;

    int remainder = (sizeof(colour_t *) * thread_return->size) % 64;

    if (remainder == 0)
    {
        real_size = sizeof(colour_t *) * thread_return->size;
    } 
    else
    {
        real_size = sizeof(colour_t *) * thread_return->size + NUMBER_OF_MACHINE_BYTES - remainder;
    }

    thread_return->pixel_matrix = (colour_t **)malloc(real_size);    

    for (int j = thread->begin; j >= thread->end; --j)
    {
        // fprintf(stderr, "\rThread %d: lines remaining: %d\n", thread->tid, (j - thread->end + 1));
        // fflush(stderr);
                
        thread_return->pixel_matrix[thread->begin - j] = (colour_t *)malloc(sizeof(colour_t) * GLOBAL_IMAGE_WIDTH);
        
        for (int i = 0; i < GLOBAL_IMAGE_WIDTH; ++i)
        {            
            colour_t pixel = colour(0, 0, 0);
            for (int s = 0; s < GLOBAL_NUMBER_OF_SAMPLES; ++s)
            {
                double u = (double)(i + rt_random_double(0, 1)) / (GLOBAL_IMAGE_WIDTH - 1);
                double v = (double)(j + rt_random_double(0, 1)) / (GLOBAL_IMAGE_HEIGHT - 1);

                ray_t ray = rt_camera_get_ray(GLOBAL_CAMERA, u, v);
                vec3_add(&pixel, ray_colour(&ray, GLOBAL_WORLD, GLOBAL_SKYBOX, GLOBAL_CHILD_RAYS));
            }
            
            thread_return->pixel_matrix[thread->begin - j][i] = pixel;
        }
    }
    
    // fprintf(stderr, "\rThead %d: DONE\n", thread->tid);
            
    pthread_exit(thread_return);
}

void setGLOBALS(const int IMAGE_HEIGHT, const int IMAGE_WIDTH, long number_of_samples, rt_camera_t *camera,
                rt_hittable_list_t *world, rt_skybox_t *skybox, const int CHILD_RAYS)
{
    GLOBAL_IMAGE_HEIGHT = IMAGE_HEIGHT;
    GLOBAL_IMAGE_WIDTH = IMAGE_WIDTH;
    GLOBAL_NUMBER_OF_SAMPLES = number_of_samples;
    GLOBAL_CAMERA = camera;
    GLOBAL_WORLD = world;
    GLOBAL_SKYBOX = skybox;
    GLOBAL_CHILD_RAYS = CHILD_RAYS;
}

void render(const int IMAGE_WIDTH, const int IMAGE_HEIGHT, long number_of_samples, rt_camera_t *camera,
            rt_hittable_list_t *world, rt_skybox_t *skybox, const int CHILD_RAYS, FILE *out_file)
{

    setGLOBALS(IMAGE_HEIGHT, IMAGE_WIDTH, number_of_samples, camera, world, skybox, CHILD_RAYS);

    pthread_t thread_list[NUM_THREADS];
    thread_n_lines_of_work *work_thread_list =
        (thread_n_lines_of_work *)malloc(sizeof(thread_n_lines_of_work) * NUM_THREADS);

    int slice_of_lines = IMAGE_HEIGHT / NUM_THREADS;

    for (int t = 0; t < NUM_THREADS; t++)
    {

        work_thread_list[t].tid = t;

        if (t == 0)
        {
            work_thread_list[t].begin = IMAGE_HEIGHT - 1;
        }
        else
        {
            work_thread_list[t].begin = work_thread_list[t - 1].end - 1;
        }

        if (t != NUM_THREADS - 1)
        {
            work_thread_list[t].end = work_thread_list[t].begin - (slice_of_lines - 1);
        }
        else
        {
            work_thread_list[t].end = 0;
        }

        pthread_create(&thread_list[t], NULL, &process_n_lines_per_thread, (void *)&work_thread_list[t]);
    }

    int ret;
    void* result;
    thread_work_return *thread_result;

    for (int t = 0; t < NUM_THREADS; t++)
    {
        ret = pthread_join(thread_list[t], &result);
        if (ret)
        {
            printf("ERROR; return code from pthread_join() is %d\n", ret);
            exit(ret);
        }

        thread_result = (thread_work_return*) result;

        for(int i = 0; i < thread_result->size; i++)
        {
            for (int j = 0; j < IMAGE_WIDTH; j++)
            {
                rt_write_colour(out_file, thread_result->pixel_matrix[i][j], number_of_samples);
            }
            
            free(thread_result->pixel_matrix[i]);                        
        }

        free(thread_result->pixel_matrix);
        free(thread_result);
    }
    
    free(work_thread_list);

    pthread_exit(NULL);
}

int main(int argc, char const *argv[])
{
    const char *number_of_samples_str = NULL;
    const char *scene_id_str = NULL;
    const char *file_name = NULL;
    bool verbose = false;

    //  Parse console arguments

    for (int i = 1; i < argc; ++i)
    {
        if (0 == strcmp(argv[i], "-s") || 0 == strcmp(argv[i], "--samples"))
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Fatal error: Argument '%s' doesn't have a value\n", argv[i]);
                show_usage(argv[0], EXIT_FAILURE);
            }
            number_of_samples_str = argv[++i];
            continue;
        }
        else if (0 == strcmp(argv[i], "--scene"))
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Fatal error: Argument '%s' doesn't have a value\n", argv[i]);
                show_usage(argv[0], EXIT_FAILURE);
            }
            scene_id_str = argv[++i];
            continue;
        }
        else if (0 == strcmp(argv[i], "-v") || 0 == strcmp(argv[i], "--verbose"))
        {
            verbose = true;
        }
        else if (0 == strcmp(argv[i], "-h"))
        {
            show_usage(argv[0], EXIT_SUCCESS);
        }
        else if ('-' == *argv[i])
        {
            fprintf(stderr, "Fatal error: Unknown argument '%s'\n", argv[i]);
            show_usage(argv[0], EXIT_FAILURE);
        }
        else if (i + 1 < argc)
        {
            fprintf(stderr, "Fatal error: Too many positional arguments (1 expected)\n");
            show_usage(argv[0], EXIT_FAILURE);
        }
        file_name = argv[i];
    }

    if (verbose)
    {
        fprintf(stderr, "Non-parsed parameters:\n");
        fprintf(stderr, "\t- number of samples: %s\n", number_of_samples_str);
        fprintf(stderr, "\t- scene ID:          %s\n", scene_id_str);
        fprintf(stderr, "\t- file_name:         %s\n", file_name);
    }

    // Parse resulting parameters
    long number_of_samples = 1000;
    if (NULL != number_of_samples_str)
    {
        char *end_ptr = NULL;
        number_of_samples = strtol(number_of_samples_str, &end_ptr, 10);
        if (*end_ptr != '\0')
        {
            fprintf(stderr, "Fatal error: Value of 'samples' is not a correct number\n");
            show_usage(argv[0], EXIT_FAILURE);
        }
    }
    rt_scene_id_t scene_id = RT_SCENE_SHOWCASE;
    if (NULL != scene_id_str)
    {
        scene_id = rt_scene_get_id_by_name(scene_id_str);
        if (RT_SCENE_NONE == scene_id)
        {
            fprintf(stderr, "Fatal error: Invalid scene identifier\n");
            show_usage(argv[0], EXIT_FAILURE);
        }
    }

    if (verbose)
    {
        fprintf(stderr, "Parsed parameters:\n");
        fprintf(stderr, "\t- number of samples: %ld\n", number_of_samples);
        fprintf(stderr, "\t- scene ID:          %d\n", scene_id);
        fprintf(stderr, "\t- file_name:         %s\n", file_name);
    }

    // Image parameters
    const double ASPECT_RATIO = 3.0 / 2.0;
    const int IMAGE_WIDTH = 300;
    const int IMAGE_HEIGHT = (int)(IMAGE_WIDTH / ASPECT_RATIO);
    const int CHILD_RAYS = 50;

    // Declare Camera parameters
    point3_t look_from, look_at;
    vec3_t up = point3(0, 1, 0);
    double focus_distance = 10.0, aperture = 0.0, vertical_fov = 40.0;

    // World
    rt_hittable_list_t *world = NULL;
    rt_skybox_t *skybox = NULL;

    // Select a scene from a pre-defined one
    switch (scene_id)
    {
        case RT_SCENE_RANDOM:
            look_from = point3(13, 2, 3);
            look_at = point3(0, 0, 0);
            aperture = 0.1;
            vertical_fov = 20.0;

            skybox = rt_skybox_new_gradient(colour(1, 1, 1), colour(0.5, 0.7, 1));
            world = rt_scene_random();
            break;

        case RT_SCENE_TWO_SPHERES:
            look_from = point3(13, 2, 3);
            look_at = point3(0, 0, 0);
            vertical_fov = 20.0;

            skybox = rt_skybox_new_gradient(colour(1, 1, 1), colour(0.5, 0.7, 1));
            world = rt_scene_two_spheres();
            break;

        case RT_SCENE_TWO_PERLIN_SPHERES:
            look_from = point3(13, 2, 3);
            look_at = point3(0, 0, 0);
            vertical_fov = 20.0;

            skybox = rt_skybox_new_gradient(colour(1, 1, 1), colour(0.5, 0.7, 1));
            world = rt_scene_two_perlin_spheres();
            break;

        case RT_SCENE_EARTH:
            look_from = point3(13, 2, 3);
            look_at = point3(0, 0, 0);
            vertical_fov = 20.0;

            skybox = rt_skybox_new_gradient(colour(1, 1, 1), colour(0.5, 0.7, 1));
            world = rt_scene_earth();
            break;

        case RT_SCENE_LIGHT_SAMPLE:
            look_from = point3(26, 3, 6);
            look_at = point3(0, 2, 0);
            vertical_fov = 20.0;

            skybox = rt_skybox_new_background(colour(0, 0, 0));
            world = rt_scene_light_sample();
            break;

        case RT_SCENE_CORNELL_BOX:
            look_from = point3(278, 278, -800);
            look_at = point3(278, 278, 0);

            skybox = rt_skybox_new_background(colour(0, 0, 0));
            world = rt_scene_cornell_box();
            break;

        case RT_SCENE_INSTANCE_TEST:
            look_from = point3(0, 5, -20);
            look_at = point3(0, 0, 0);
            vertical_fov = 20.0;

            skybox = rt_skybox_new_gradient(colour(1, 1, 1), colour(0.5, 0.7, 1));
            world = rt_scene_instance_test();
            break;

        case RT_SCENE_CORNELL_SMOKE:
            look_from = point3(278, 278, -800);
            look_at = point3(278, 278, 0);

            skybox = rt_skybox_new_background(colour(0, 0, 0));
            world = rt_scene_cornell_box_smoke_boxes();
            break;

        case RT_SCENE_SHOWCASE:
            look_from = point3(478, 278, -600);
            look_at = point3(278, 278, 0);

            skybox = rt_skybox_new_background(colour(0, 0, 0));
            world = rt_scene_showcase();
            break;

        case RT_SCENE_METAL_TEST:
            look_from = point3(0, 5, -10);
            look_at = point3(0, 2, 0);
            vertical_fov = 20.0;

            skybox = rt_skybox_new_gradient(colour(1, 1, 1), colour(0.5, 0.7, 1));
            world = rt_scene_metal_test();
            break;
        case RT_SCENE_NONE:
            fprintf(stderr, "Fatal error: scene id is undefined after parsing the parameters\n");
            return EXIT_FAILURE;
    }

    rt_camera_t *camera =
        rt_camera_new(look_from, look_at, up, vertical_fov, ASPECT_RATIO, aperture, focus_distance, 0.0, 1.0);

    FILE *out_file = stdout;
    if (NULL != file_name)
    {
        out_file = fopen(file_name, "w");
        if (NULL == out_file)
        {
            fprintf(stderr, "Fatal error: Unable to open file %s: %s", file_name, strerror(errno));
            goto cleanup;
        }
    }

    // Render
    fprintf(out_file, "P3\n%d %d\n255\n", IMAGE_WIDTH, IMAGE_HEIGHT);
    render(IMAGE_WIDTH, IMAGE_HEIGHT, number_of_samples, camera, world, skybox, CHILD_RAYS, out_file);
    fprintf(stderr, "\nDone\n");
cleanup:
    // Cleanup
    rt_hittable_list_deinit(world);
    rt_camera_delete(camera);
    rt_skybox_delete(skybox);    

    return EXIT_SUCCESS;
}

static void show_usage(const char *program_name, int err)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "%s [-s|--samples N] [--scene SCENE] [-v|--verbose] [output_file_name]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "\t-s | --samples      <int>       Number of rays to cast for each pixel\n");
    fprintf(
        stderr,
        "\t--scene             <string>    ID of the scene to render. List of available scenes is printed below.\n");
    fprintf(stderr, "\t-v | --verbose                  Enable verbose output\n");
    fprintf(stderr, "\t-h                              Show this message and exit\n");
    fprintf(stderr, "Positional arguments:\n");
    fprintf(stderr,
            "\toutput_file_name                Name of the output file. Outputs image to console if not specified.\n");
    fprintf(stderr, "Available scenes:\n");
    rt_scene_print_scenes_info(stderr);

    exit(err);
}
