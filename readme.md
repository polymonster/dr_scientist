# dr_scientist [![CircleCI](https://circleci.com/gh/polymonster/dr_scientist.svg?style=svg&circle-token=4947316ae6ba47bee765c79654982d990b1dc5be)](https://circleci.com/gh/polymonster/dr_scientist)

The project builds ontop of [pmtech](https://github.com/polymonster/pmtech.git)... please checkout pmtech first and get familiar with how to build

It was a work in progress game that might never be completed, this is being made public as reference for pmtech and how to setup more advanced features: 
- character controller 
- kinematic physics controller
- ecs extensions
- physics ray/ sphere casts for controllers
- collectable items

<img src="https://github.com/polymonster/polymonster.github.io/blob/da8757c5d9e8a142f0f4ef4a83c486109467e7c1/images/pmtech/gifs/dr_scientist.gif" width="1280" />

# Requirements 

requirements:   
[python3](https://www.python.org/download/releases/3.0)  
[git lfs](https://git-lfs.github.com/)  
[pmtech](https://github.com/polymonster/pmtech.git) to be located one directory above this one (../pmtech)  

# Building 

be sure to update or clone pmtech before running:

```
cd ../pmtech
git pull
git submodule update --init recursive
```
use pmbuild `-help` to see what profiles you can build:

```
pmbuild -help
```

to generate projects (xcode, visual studio) and data to build with 

```
cd dr_scientist
pmbuild <platform> (mac, win32, linux)
```

to build and run from the commandline

```
pmbuild make <platform> dr_scientist -<compiler args>
pmbuild launch <platform> dr_scientist
```

# Controls 

WASD or Left Stick to move.
Q or Button X/A to Jump



