# Real-time Realistic 3D Graphics Rendering using DirectX Raytracing (DXR)

This project was developed as an engineering thesis at the Warsaw University of Technology, Faculty of Mathematics and Information Sciences.

**Authors:**
* Adrian Paluch
* Jan Machalski

**Supervisor:**
* dr inż. Paweł Kotowski

## Project Overview

The aim of this project was to design and implement a real-time rendering engine utilizing hardware-accelerated ray tracing via the **DirectX 12** API and its **DirectX Raytracing (DXR)** extension. The application demonstrates the practical application of modern rendering techniques to achieve high visual fidelity in interactive environments.

### Key Features
* **Hybrid Rendering Engine:** Support for rasterization, full ray tracing, and combined (hybrid) modes.
* **Physically Based Rendering (PBR):** Implementation of materials based on the PBR model (Albedo, Roughness, Metalness).
* **Advanced Light Sampling:** Implementation of **Resampled Importance Sampling (RIS)** with **Weighted Reservoir Sampling (WRS)** for efficient lighting calculations.
* **NVIDIA DLSS Integration:** Full support for DLSS 3.5, including **Ray Reconstruction** and **Super Resolution** for improved performance and image quality.
* **Visual Effects:** Realistic shadows, reflections, refraction (utilizing Beer-Lambert law for absorption), and recursive ray tracing.

## Building the Project

To configure and generate the project files, run the following command in the root directory:

```
cmake -S . -B build
```

This command will automatically download all the required DLLs and libraries needed for the project.

## Thesis Document

You can read the full engineering thesis (in Polish) by clicking the link below:

### [Read the Engineering Thesis (PDF)](https://github.com/user-attachments/files/26857491/RayTracing.pdf)


## Gallery
<img width="2560" height="1369" alt="apartament2" src="https://github.com/user-attachments/assets/053fe54c-3396-48ee-9618-670bba5d5af8" />
<br>
<img width="2434" height="1369" alt="beaut_game1" src="https://github.com/user-attachments/assets/2a5d6aa9-9365-4d9b-8886-99e41561ddaf" />
<br>
<img width="2434" height="1369" alt="kitchen1" src="https://github.com/user-attachments/assets/31dd9477-1397-41ac-af7e-27c7cdab059b" />
<br>
<img width="2434" height="1369" alt="sponza_tree1" src="https://github.com/user-attachments/assets/be31c5ad-c2ae-43fe-85d7-001e3b74eaff" />


