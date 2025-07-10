Usage:
1- put the gamefiles for the depot you want to patch in the "source" folder (wherever the py is /source)
2- put the gamefiles for the compatible version in the "target" folder (wherever the py is /target)
3- run creatediff.py (you will need to install the dependencies, mainly bsdiff)
4- wait like 30 mins, lol
5- after its done, create a version.txt file containing the version of the game that the patch should target (ex: "v.X.X.X.XXXX")
6- copy the contents of the output folder to the patch folder in the root of the project so they get copied next to the exe when you build
