C:\VulkanSDK\1.3.250.1\Bin\glslc.exe --target-env=vulkan1.2 test.mesh -o test.mesh.spv
C:\VulkanSDK\1.3.250.1\Bin\glslc.exe --target-env=vulkan1.2 test.frag -o test.frag.spv
C:\VulkanSDK\1.3.250.1\Bin\glslc.exe --target-env=vulkan1.2 default.mesh -o default.mesh.spv
C:\VulkanSDK\1.3.250.1\Bin\glslc.exe --target-env=vulkan1.2 default.frag -o default.frag.spv
C:\VulkanSDK\1.3.250.1\Bin\glslc.exe --target-env=vulkan1.2 default.task -o default.task.spv
C:\VulkanSDK\1.3.250.1\Bin\glslc.exe -S --target-env=vulkan1.2 default.task -o default-asm.task.spv
pause