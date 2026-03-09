## This template requires Prisma UI and nodejs

- [Prisma UI](https://www.prismaui.dev/getting-started/installation/)
- [NodeJS](https://nodejs.org/pt-br/download)
- You might also want to install the svelte extesion for visual stuio or vscode

## DEV Server

By default when building with the debug target, the static html files will not be generated, instead and dev server will be used.

You can disable it by setting the following command to off

```cmake
set(DEV_SERVER OFF CACHE BOOL " " FORCE)
```

to start the dev server you can run the following commands on the root directory

every time
```ps
cd ui
```

only the first time
```ps
npm install
```

every time
```ps
npm run dev
```

## If you want to create a mod based on this project you should:

Modify CMakeLists.txt with your name and the name of your mod
```cmake
set(AUTHOR_NAME "AuthorName")
set(PRODUCT_NAME "PrismaUISKSETemplate")
set(BEAUTIFUL_NAME "PrismaUI SKSE Template")
```

Delete the LICENCE file and use git to create the licence you want your mod to be
<img width="572" height="292" alt="image" src="https://github.com/user-attachments/assets/80cd698b-a4ac-499d-9de3-07d261682604" />
<img width="499" height="182" alt="image" src="https://github.com/user-attachments/assets/2cb7e4c8-edd6-4b24-91d8-074d2701c951" />

Update the cmake/version.rc.in with your licence (This licence will go into the dll)

## Environment variables

[How to set up envioriment variables](https://gist.github.com/Thiago099/b45ec7832fb754325b29a61006bcd10c)

COMMONLIB_SSE_FOLDER

Clone this Repository, to somewhere safe and adds its path to this environment variable on Windows.

```bash
git clone --recursive https://github.com/alandtse/CommonLibVR
cd CommonLibVR
git checkout ng
```
  
## Optional ouput folder optional variables

- SKYRIM_FOLDER
- WILDLANDER_OWRT_FOLDER
- SKYRIM_OWRT_FOLDER
- SKYRIM_MODS_FOLDER2
- SKYRIM_MODS_FOLDER
