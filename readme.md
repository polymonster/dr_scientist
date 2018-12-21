# dr_scientist [![Build Status](https://travis-ci.org/polymonster/dr_scientist.svg?branch=master)](https://travis-ci.org/polymonster/dr_scientist) [![Build Status](https://ci.appveyor.com/api/projects/status/github/polymonster/dr_scientist?branch=master&svg=true&passingText=win32-passing&pendingText=win32-pending&failingText=win32-failing)](https://ci.appveyor.com/project/polymonster/dr-scientist)

Follow work in progress: [trello](https://trello.com/b/MC3BGwCk/dr-scientist)

The project builds ontop of [pmtech](https://github.com/polymonster/pmtech.git). 

It is developed by 2 experienced games industry professionals, one coder, one artists. We both work full time on other things so this project is a slow burner.

![alt text](images/ds.png)

requirements:   
[python3](https://www.python.org/download/releases/3.0)  
[git lfs](https://git-lfs.github.com/)  
[pmtech](https://github.com/polymonster/pmtech.git) to be located one directory above this one (../pmtech)  

be sure to update or clone pmtech before running:
```
cd ../pmtech
git submodule init
git submodule update
git pull
```

to generate projects and binaries run:  
```
# win32
build.bat

# macOS and linux
../pmtech/pmbuild -all
```



