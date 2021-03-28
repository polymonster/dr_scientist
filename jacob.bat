cd ../pmtech
git pull
git submodule update --init --recursive
cd ../dr_scientist
git pull
..\\pmtech\\pmbuild win32 -libs && ..\\pmtech\\pmbuild win32
