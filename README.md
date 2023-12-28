# Second Capture

This is a simple DIY tool that uses the Windows desktop API to capture display output, once every second or every few second, as a DirectX texture and compress it on the GPU directly to h265.

Configuration is done by adjusting the preprocessor definitions at the start of `second_capture.cpp`. You can adjust the output folder, where the capture is stored as AVI, capture interval, graphics adapter and monitor, and playback speed.

Running the tool in Debug mode will show a console window and list all the available adapter and monitors, which you can copy there. In Release mode, the tool will just run forever in the background without any UI. If you have multiple monitors, compile one variation of the tool for each monitor, and run all of them.

After you've confirmed it's working on your configuration, just drop the compiled Release exe in some folder somwhere, launch it, and add it to the startup programs if you want it to run automatically.

Anything that causes the capture source to get lost, e.g. UAC or login screen, will end the capture, and start a new file afterwards as soon as the display output is available again. The tool doesn't crash.

Good luck!