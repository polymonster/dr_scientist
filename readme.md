# dr_scientist [![CircleCI](https://circleci.com/gh/polymonster/dr_scientist.svg?style=svg&circle-token=4947316ae6ba47bee765c79654982d990b1dc5be)](https://circleci.com/gh/polymonster/dr_scientist)

The project builds ontop of [pmtech](https://github.com/polymonster/pmtech.git)... please checkout pmtech first and get familiar with how to build

It was a work in progress game that might never be completed, this is being made public as reference for pmtech and how to setup and use character controllers, physics and ecs extensions which are not in the pmtech examples.

![alt text](images/ds.png)

requirements:   
[python3](https://www.python.org/download/releases/3.0)  
[git lfs](https://git-lfs.github.com/)  
[pmtech](https://github.com/polymonster/pmtech.git) to be located one directory above this one (../pmtech)  

be sure to update or clone pmtech before running:
```
cd ../pmtech
git pull
git submodule update --init recursive
```

to generate projects and binaries run from this directory:  
```
pmbuild <platform> (mac, win32, linux)
```




