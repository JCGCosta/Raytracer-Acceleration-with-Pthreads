# Raytracer Acceleration with Pthreads

For more infos about the project see [practical-work-pthreads](./practical-work-pthreads.pdf)

#### To Run The Serial Version

First you need install some tools:

-   ninja
-   Cmake    
-   xgd-ultils

**To install in Ubuntu:**

``` bash
sudo apt-get update
```

``` bash
apt-get install ninja-build
```

``` bash
sudo apt-get cmake`
```

**To Run (Build, Execute and Show):**

``` bash
cd raytracing-serial-1
```

``` bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release..
```

``` bash
ninja
```

``` bash
./ray_tracing_one_week > image.ppm
```

``` bash
xdg-open image.ppm
```