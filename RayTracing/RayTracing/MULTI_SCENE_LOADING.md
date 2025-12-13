# Multi-Scene Loading Feature

## Overview
This document describes the multi-scene loading feature that allows users to load multiple 3D scene files simultaneously (e.g., Sponza palace + curtains + additional assets).

## Features

### 1. Multi-File Selection Dialog
- Users can select multiple scene files at once using Windows file dialog
- Supports GLTF, GLB, OBJ, and FBX formats
- Files are displayed in an organized list with options to add/remove

### 2. Scene Management Window
The scene selection window (`RenderMultiSceneSelectionWindow`) provides:
- **List view** with numbered entries showing:
  - Filename
  - Parent directory hint
  - Full path tooltip on hover
- **Add More...** button - add additional files to the list
- **Clear All** button - remove all files from the list
- **Remove** button for each file - remove individual files
- **Load Selected** button - start loading all selected scenes

### 3. Memory Management
The system handles GPU memory limitations gracefully:
- Scenes are loaded **in order** as specified by the user
- If GPU memory limit is reached, loading stops
- Remaining scenes are skipped
- User receives a warning dialog showing:
  - How many scenes were successfully loaded
  - How many were skipped due to memory constraints

### 4. Asynchronous Loading
- Scene loading happens on a background thread
- Loading screen shows:
  - First filename + count of additional scenes
  - Animated loading indicator
  - Helpful tip about large scenes
- Main thread remains responsive during loading

### 5. Error Handling
The system includes robust error handling:
- **Per-scene error handling**: If one scene fails, others continue
- **Memory allocation failures**: Caught and handled gracefully
- **GPU upload errors**: Attempt recovery with partial data
- **User notifications**: Warning dialogs for all error conditions

## Usage

### Loading Multiple Scenes

1. **From Main Menu**:
   - Click "Load Scene(s)..."
   - Select one or more files in the file dialog
   - Scene selection window opens

2. **From Pause Menu**:
   - Press ESC to open pause menu
   - Click "Load Different Scene(s)..."
   - Select files and manage list

3. **Managing Scene List**:
   - Add more files with "Add More..." button
   - Remove individual files with "Remove" button next to each entry
   - Clear entire list with "Clear All" button
   - Load when ready with "Load Selected" button

4. **During Loading**:
   - Loading screen displays progress
   - Background thread loads scenes asynchronously
   - Console shows detailed progress for each scene

5. **After Loading**:
   - If all scenes loaded: Scene mode activates normally
   - If some scenes skipped: Warning dialog explains situation
   - Click OK to dismiss warning and continue

## Technical Details

### Code Components

#### UIManager (`UIManager.h/cpp`)
- `RenderMultiSceneSelectionWindow()` - Scene list management UI
- `RenderWarningWindow()` - Memory limit warnings
- `OpenMultiFileDialog()` - Windows multi-file selection dialog
- `ShowWarning()` - Display warning messages

#### Application (`Application.h/cpp`)
- `LoadMultipleScenes()` - Initiates multi-scene loading
- `PerformAsyncMultiLoad()` - Background thread loading
- `UploadModelsToGPU()` - GPU upload with memory checking

#### Renderer (`Renderer.h/cpp`)
- `LoadMultipleScenes()` - GPU resource allocation
- Memory-safe model loading with exception handling
- Graceful degradation on allocation failures

### Memory Handling Strategy

1. **CPU Loading Phase** (Background Thread):
   - Load scene files using Assimp
   - Parse geometry and materials
   - No GPU resources allocated yet

2. **GPU Upload Phase** (Main Thread):
   - Try to add each model to `mModels` vector
   - Catch `std::bad_alloc` exceptions
   - Stop loading on first memory failure
   - Keep successfully loaded models

3. **Resource Building**:
   - Build mesh GPU data (vertex/index buffers)
   - Create textures and materials
   - Build ray tracing acceleration structures
   - Catch failures in each step, continue with partial data

4. **User Notification**:
   - Compare loaded count vs. requested count
   - Show warning if any scenes were skipped
   - Display in-scene warning dialog

## Example Use Cases

### Sponza Scene with Extensions
```
1. Main palace: Sponza.gltf
2. Curtains: Sponza_Curtains.gltf
3. Plants: Sponza_Plants.gltf
4. Flags: Sponza_Flags.gltf
```

### Modular Environment
```
1. Base terrain: Terrain.fbx
2. Buildings: Buildings_LOD0.fbx
3. Vegetation: Trees.fbx
4. Props: Props_Set1.obj
```

## Limitations

- **Order matters**: Scenes are loaded in list order
- **No partial scene loading**: Each scene is all-or-nothing
- **Memory estimation**: No pre-check for memory requirements
- **Fixed priority**: Cannot specify which scenes are most important

## Future Improvements

Potential enhancements for this feature:
1. **Pre-loading memory estimation** - Check approximate size before loading
2. **Priority system** - Mark essential vs. optional scenes
3. **Streaming** - Load/unload scenes dynamically based on camera position
4. **Progress bar** - Show per-scene loading progress
5. **Scene groups** - Save/load predefined scene combinations
6. **Memory budget UI** - Show estimated vs. available memory

## Console Output Example

```
Loading 3 scenes in background thread...
Loading [1/3]: C:\Sponza\Sponza.gltf
  -> Success (100 meshes)
Loading [2/3]: C:\Sponza\Sponza_Curtains.gltf
  -> Success (25 meshes)
Loading [3/3]: C:\Sponza\Sponza_Plants.gltf
  -> Success (50 meshes)
Background multi-loading completed: 3 / 3 scenes loaded successfully
Uploading models to GPU...
Successfully added model 1 / 3
Successfully added model 2 / 3
Successfully added model 3 / 3
Added 3 / 3 models to scene.
Mesh GPU data built in 2.34 seconds.
Ray tracing structures built in 1.12 seconds.
All 3 scenes loaded successfully!
```

## Troubleshooting

### "Failed to load scenes" error
- Check if scene files exist and are valid
- Verify file formats are supported (GLTF, GLB, OBJ, FBX)
- Look at console output for specific error messages

### Partial loading (not all scenes loaded)
- This is expected behavior when GPU memory is full
- Try loading fewer scenes at once
- Close other GPU-intensive applications
- Consider scenes with fewer polygons/textures

### Loading hangs
- Large scenes take time to load
- Check console for progress updates
- Wait for loading screen to complete
- If truly stuck, restart application

