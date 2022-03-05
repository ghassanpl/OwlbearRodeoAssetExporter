# OwlbearRodeoAssetExporter

A simple tool to export assets (currently only map images) from .owlbear files as created by [Owlbear Rodeo](https://owlbear.rodeo) exports.

## Usage

OwblearRodeoAssetExporter.exe <filename.owlbear>

It will create a .json and .png/.jpeg/.webp pair for each map image in the .owlbear file. It will store them next to the .owlbear file so make sure that directory is writeable. The names of the output files will be based on the map names.

## TODO

These are **possible** changes one could make to make this tool better:

- export entire directories
- choose output paths
- disable .json output
- better mime type detection and extension adding
- exporting tokens as well as maps
- choosing what to export (which assets) via flags and filters
- better error handling
  - some maps can have names that cannot be represented in the filesystem
- support non-Windows OSes (only mmap.cpp is windows-specific)