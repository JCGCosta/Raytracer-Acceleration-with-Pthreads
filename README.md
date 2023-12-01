# Raytracer Acceleration with Pthreads

For more infos about the project see [practical-work-pthreads](./practical-work-pthreads.pdf)

## To Run The Serial Version

First you need extract the `raytracing-serial-1.zip`.

Second you need install some tools:

-   ninja
-   Cmake    
-   xgd-ultils

**To install in Ubuntu:**

``` bash
sudo apt-get update
```

``` bash
sudo apt-get install ninja-build
```

``` bash
sudo apt-get cmake
```

Third you need execute the commands a follow:

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

## To Analysis the Version using the Valgrind

First you need install the **Valgrind**:

**To install in Ubuntu:**

``` bash
sudo apt-get install valgrind
```

Second you need compile and execute with debugging information

**Serial Version:**

Go to the Serial Code Folder and execute these commands:

``` bash
valgrind --tool=callgrind ./ray_tracing_one_week > image.ppm
``` 

``` bash
kcachegrind callgrind.out.PID
```

**PID**: Is a randon number that represents the Process ID
