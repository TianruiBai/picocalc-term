# PicoCalc multi booter

Here is a bootloader for PicoCalc combined slightly modified [PicoMite](https://github.com/madcock/PicoMiteAllVersions) and [SD boot](https://github.com/adwuard/Picocalc_SD_Boot)   

- Pico1 
- No sdcard inserted,load default app to run from flash. 
- Sdcard inserted, SD boot menu will show up, load third pico app bin to run at FLASH TARGET OFFSET 2048k-200k

## How to compile
```
git clone --recursive https://github.com/clockworkpi/PicoCalc.git
cd PicoCalc/Code/pico_multi_booter
export PICO_SDK_PATH=/where/picosdk/is
mkdir build
cd build
cmake ..
make
```
## How to run 

Just copy **combined.uf2** into PicoCalc


## PicoMite
configuration.h
```
#define FLASH_TARGET_OFFSET (968 * 1024)
```

## sd_boot

config.h
```
#define SD_BOOT_FLASH_OFFSET         (200 * 1024)
```

### SD Card Application Build and Deployment
**Important Note:**   
```
Applications intended for SD card boot "MUST REBUILD" using a custom linker script to accommodate the program's offset(200k) address.

Applications intended for SD card boot is in **bin** format, not uf2.
 
```

--- 
This section explains how to build and deploy applications on an SD card. Below is a simple example of a CMakeLists.txt for an application.


#### Step 1 Copy Custom Link Script
Copy `memmap_default_rp2040.ld` and `memmap_default_rp2350.ld` to your project repository.


#### Step 2 Add Custom Link Script to CMakeList.txt
In the project for multi boot, add custom link script to CMakeList.txt,usually at bottom.   
We also need to use `pico_add_extra_outputs` to sure that there will .bin file generated.   

```
...
pico_add_extra_outputs(${CMAKE_PROJECT_NAME})
...

function(enable_sdcard_app target)
  if(${PICO_PLATFORM} STREQUAL "rp2040")
    pico_set_linker_script(${CMAKE_PROJECT_NAME} ${CMAKE_SOURCE_DIR}/memmap_default_rp2040.ld)
  elseif(${PICO_PLATFORM} MATCHES "rp2350")
    pico_set_linker_script(${CMAKE_PROJECT_NAME} ${CMAKE_SOURCE_DIR}/memmap_default_rp2350.ld)
  endif()
endfunction()

enable_sdcard_app(${CMAKE_PROJECT_NAME})
```
#### Build and Deployment Process
1. Build the project using the above CMakeLists.txt.

```bash
mkdir build; 
cd build
export PICO_SDK_PATH=/path/to/pico-sdk 
cmake ..
make
```

#### Step 3 Your Custom Application Is Ready For SD Card Boot 
Once the build is complete, copy the generated `.bin` file to the `/firmware` directory of the SD card.  



