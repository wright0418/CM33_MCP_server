# Install script for directory: C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/SampleCode/StdDriver

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/install/armclang")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "CMAKE_OBJDUMP-NOTFOUND")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/armclang/SampleCode/StdDriver/FMC_DualBankFwUpdate/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/armclang/SampleCode/StdDriver/FMC_FwUpdateApplication/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/armclang/SampleCode/StdDriver/HSOTG_Dual_Role_UMAS/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/armclang/SampleCode/StdDriver/HSOTG_HNP/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/armclang/SampleCode/StdDriver/HSUSBH_USBH_Firmware_Update/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/armclang/SampleCode/StdDriver/HSUSBH_USBH_HID_Keyboard/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/armclang/SampleCode/StdDriver/HSUSBH_USBH_HID_Mouse_Keyboard/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/armclang/SampleCode/StdDriver/HSUSBH_USBH_MassStorage/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/armclang/SampleCode/StdDriver/HSUSBH_USBH_UAC_LoopBack/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/armclang/SampleCode/StdDriver/HSUSBH_USBH_VENDOR_LBK/cmake_install.cmake")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/cfwu3/Desktop/Nuvoton_BSP/M3331/build/armclang/SampleCode/StdDriver/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
