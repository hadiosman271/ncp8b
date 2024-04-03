# ncp8b

## (unfinished)
[Demo](ncp8b_demo.gif)

A video player that renders frames as ASCII art, using the ffmpeg API for video decoding and ncurses for terminal manipulation.  
ncp8b is an acronym for '**nc**urses **p**layer, **8** **b**it' ~~(output is in 8 bit color)~~.  
Since this uses ffmpeg, many video formats should work.  

#### Building
Build with make:  
`make`  
libav* (4.2.2) and ncurses must be installed.  

(TODO: cmake)  

~~Generate build files:~~  
~~`cmake -S . -B build/`~~  
~~libav* (4.2.2) and ncurses must be installed.~~  

~~Build:~~  
~~`cmake --build build/`~~  

~~`build/` will contain the binary. It doesn't depend on anything relative to its location so you can move it wherever you want.~~  

#### Usage
Run with an argument to specify the video file to open, e.g:  
`ncp8b [video_file_path]`  

(TODO)  

~~If no argument is given, a file can still be opened while the program is running by typing:~~  
~~`:[video_file_path]`~~  

~~Use space to pause/play, arrow keys to seek, and tab to switch tabs.~~  
~~On the logs tab, vim-like controls can be used.~~  
