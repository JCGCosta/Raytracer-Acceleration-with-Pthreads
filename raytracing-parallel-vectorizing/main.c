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

// Global reading variables
int IMAGE_WIDTH_global;
int IMAGE_HEIGHT_global;
long number_of_samples_global;
rt_camera_t *camera_global;
rt_hittable_list_t *world_global;
rt_skybox_t *skybox_global;
int CHILD_RAYS_global;

// Global writing variables
long thread_flag[NUM_THREADS];
pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;

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

// Changes start here

typedef struct {
	int thread_id;
	int cur_line;
	colour_t *line_res;
} thread_work;

void set_globals(int IMAGE_WIDTH, int IMAGE_HEIGHT, long number_of_samples, rt_camera_t *camera, rt_hittable_list_t *world, rt_skybox_t *skybox, int CHILD_RAYS)
{
	IMAGE_HEIGHT_global = IMAGE_HEIGHT;
	IMAGE_WIDTH_global = IMAGE_WIDTH;
	number_of_samples_global = number_of_samples;
	camera_global = camera;
	world_global = world;
	skybox_global = skybox;
	CHILD_RAYS_global = CHILD_RAYS;
}

void thread_flag_init(long *thread_flag) 
{
	for (int i = 0; i < NUM_THREADS; i++) thread_flag[i] = 0;
}

void *process_line_thread(void *work)
{
	thread_work *cur_work = (thread_work *)work;
	colour_t *local_work_res = (colour_t *)malloc(IMAGE_WIDTH_global * sizeof(colour_t));
	
	for (int i = 0; i < IMAGE_WIDTH_global; ++i)
        {
		colour_t pixel = colour(0, 0, 0);
		for (int s = 0; s < number_of_samples_global; ++s)
		{
			double u = (double)(i + rt_random_double(0, 1)) / (IMAGE_WIDTH_global - 1);
			double v = (double)(cur_work->cur_line + rt_random_double(0, 1)) / (IMAGE_HEIGHT_global - 1);

			ray_t ray = rt_camera_get_ray(camera_global, u, v);
			vec3_add(&pixel, ray_colour(&ray, world_global, skybox_global, CHILD_RAYS_global));
		}
		local_work_res[i] = pixel;
	}
	
	cur_work->line_res = local_work_res;
	
	// Preventing false sharing problem
	pthread_mutex_lock(&thread_flag_mutex);
	thread_flag[cur_work->thread_id] = 0;
	pthread_mutex_unlock(&thread_flag_mutex);
	
	pthread_exit(NULL);
}

void despatch_line(const int IMAGE_WIDTH, colour_t *line_res, FILE *out_file, long number_of_samples)
{
	for (int i = 0; i < IMAGE_WIDTH; ++i)
	{
		rt_write_colour(out_file, line_res[i], number_of_samples);
	}
}

bool has_work_already()
{
	for (int t = 0; t < NUM_THREADS; ++t)
	{
		if (thread_flag[t] == 1) return true;
	}
	return false;
}

void render(const int IMAGE_WIDTH, const int IMAGE_HEIGHT, long number_of_samples, rt_camera_t *camera, rt_hittable_list_t *world, rt_skybox_t *skybox, const int CHILD_RAYS, FILE *out_file)
{
	// Initial setup
	pthread_t threads[NUM_THREADS];
	long cur_line = IMAGE_HEIGHT-1; // (image height = 200) (vectors go from 0 to 199)
	
	// Start global variables and the thread_flag with 0
	set_globals(IMAGE_WIDTH, IMAGE_HEIGHT, number_of_samples, camera, world, skybox, CHILD_RAYS);
	thread_flag_init(thread_flag);
	
	// Work vector for the threads to delivery the results
	thread_work *work = (thread_work *)malloc(IMAGE_HEIGHT * sizeof(thread_work));
	work[0].line_res = NULL;
	
	// Until reach the image end
	while (work[0].line_res == NULL)
	{
		for (int t = 0; t < NUM_THREADS; ++t)
		{			
			if (thread_flag[t] == 0 && cur_line >= 0)
			{
				pthread_mutex_lock(&thread_flag_mutex);
				thread_flag[t] = 1; // Flag says that the current thread is busy
				pthread_mutex_unlock(&thread_flag_mutex);
				
				work[cur_line].thread_id = t;
				work[cur_line].cur_line = cur_line;
				work[cur_line].line_res = NULL;
				
				pthread_create(&threads[t], NULL, &process_line_thread, (void *)&work[cur_line]);
				cur_line--;
			}
		}	
	}
	
	// All threads finished their work
	while(has_work_already());
	
	// Dispatch work to the file	
	for (int l = IMAGE_HEIGHT-1; l >= 0; --l)
	{
		despatch_line(IMAGE_WIDTH, work[l].line_res, out_file, number_of_samples_global);
	}
}

// Changes finish here

int main(int argc, char const *argv[])
{
    const char *number_of_samples_str = NULL;
    const char *scene_id_str = NULL;
    const char *file_name = NULL;
    bool verbose = false;
    // Parse console arguments
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
    fprintf(stderr, "\t--scene             <string>    ID of the scene to render. List of available scenes is printed below.\n");
    fprintf(stderr, "\t-v | --verbose                  Enable verbose output\n");
    fprintf(stderr, "\t-h                              Show this message and exit\n");
    fprintf(stderr, "Positional arguments:\n");
    fprintf(stderr, "\toutput_file_name                Name of the output file. Outputs image to console if not specified.\n");
    fprintf(stderr, "Available scenes:\n");
    rt_scene_print_scenes_info(stderr);

    exit(err);
}
