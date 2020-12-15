#!/usr/bin/env bash

set -e

VLC_VERSION=3.0.8
SPARKLE_VERSION=1.23.0
LIBCAFFEINE_VERSION=0.6.1
QT_VERSION=5.14.1
CEF_BUILD_VERSION=75.1.14+gc81164e+chromium-75.0.3770.100
OBSDEPS_VERSION=2020-04-24

if ! which brew >> /dev/null
then
  echo Installing homebrew
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
fi

echo Installing dependencies from homebrew
brew update --preinstall
brew bundle --file ./CI/scripts/macos/Brewfile


echo Checking for VLC
VLC_PATH=$PWD/vlc-${VLC_VERSION}
if [ ! -d $VLC_PATH ]
then
  [ -e ./vlc-${VLC_VERSION}.tar.xz ] || \
    curl -L -O https://downloads.videolan.org/vlc/${VLC_VERSION}/vlc-${VLC_VERSION}.tar.xz
  echo Uncompressing VLC
  tar xf vlc-${VLC_VERSION}.tar.xz
  rm vlc-${VLC_VERSION}.tar.xz
fi

echo Checking for Sparkle
if [ ! -d cmbuild/sparkle/Sparkle.framework ]
then
  mkdir -p cmbuild/sparkle
  [ -e ./sparkle.tar.bz2 ] || \
    curl -L -o sparkle.tar.bz2 https://github.com/sparkle-project/Sparkle/releases/download/${SPARKLE_VERSION}/Sparkle-${SPARKLE_VERSION}.tar.bz2
  echo Uncompressing sparkle
  tar xf ./sparkle.tar.bz2 -C cmbuild/sparkle
  rm ./sparkle.tar.bz2
fi

[ -d /Library/Frameworks/Sparkle.framework ] || \
  sudo cp -R cmbuild/sparkle/Sparkle.framework /Library/Frameworks/Sparkle.framework

echo Checking for OBS Project Dependencies
OBSDEPS_DIR=$PWD/cmbuild/obsdeps
if [ ! -d $OBSDEPS_DIR ]
then
  pushd cmbuild
  [ -e ./osx-deps-${OBSDEPS_VERSION}.tar.gz ] || \
  curl -L -O https://github.com/obsproject/obs-deps/releases/download/$OBSDEPS_VERSION/osx-deps-${OBSDEPS_VERSION}.tar.gz
  echo Uncompressing OBS dependencies
  tar xf ./osx-deps-${OBSDEPS_VERSION}.tar.gz
  rm ./osx-deps-${OBSDEPS_VERSION}.tar.gz
  popd
fi
[ -e /tmp/obsdeps ] || ln -s $OBSDEPS_DIR /tmp/obsdeps

echo Checking for libcaffeine
LIBCAFFEINE_DIR=$PWD/libcaffeine-v${LIBCAFFEINE_VERSION}-macos
if [ ! -d $LIBCAFFEINE_DIR ]
then
  [ -e ./libcaffeine-v${LIBCAFFEINE_VERSION}-macos.7z ] || \
    curl -L -O https://github.com/caffeinetv/libcaffeine/releases/download/v${LIBCAFFEINE_VERSION}/libcaffeine-v${LIBCAFFEINE_VERSION}-macos.7z
  which 7z >> /dev/null || brew install p7zip
  echo Uncompressing libcaffeine
  7z x libcaffeine-v${LIBCAFFEINE_VERSION}-macos.7z
  rm libcaffeine-v${LIBCAFFEINE_VERSION}-macos.7z
fi

CEF_BASENAME=cef_binary_${CEF_BUILD_VERSION}_macosx64
CEF_BASENAME_MINIMAL=${CEF_BASENAME}_minimal

if [ -z "${CEF_ROOT_DIR}" ]
then
  CEF_ROOT_DIR=$PWD/cmbuild/${CEF_BASENAME_MINIMAL}
  echo "export CEF_ROOT_DIR=$CEF_ROOT_DIR" >> $HOME/.zshrc
fi

which cmake >> /dev/null || brew install cmake

if [ ! -e ./cmbuild/${CEF_BASENAME} ]
then
  if [ ! -e ${CEF_ROOT_DIR} ]
  then
    echo "Downloading and building CEF (this will take a while)"
    pushd cmbuild
    CEF_TARFILE=`echo ${CEF_BASENAME_MINIMAL}.tar.bz2 | sed 's/+/%2B/g'`
    if [ ! -e ./${CEF_TARFILE} ]
    then
      echo Getting http://opensource.spotify.com/cefbuilds/$CEF_TARFILE
      curl -L -O http://opensource.spotify.com/cefbuilds/$CEF_TARFILE
    fi
    echo "Uncompressing CEF"
    tar xf ./${CEF_TARFILE}
    rm ${CEF_TARFILE}
    mkdir ${CEF_BASENAME_MINIMAL}/build
    cd ${CEF_BASENAME_MINIMAL}/build
    echo "Building CEF"
    cmake -DCMAKE_CXX_FLAGS="-std=c++11 -stdlib=libc++" -DCMAKE_EXE_LINKER_FLAGS="-std=c++11 -stdlib=libc++" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.11 ..
    make -j
    popd
  fi
  ln -s ${CEF_ROOT_DIR} ./cmbuild/${CEF_BASENAME}
fi

echo Checking for QT
QT_DIR=/usr/local/Cellar/qt/$QT_VERSION
if [ ! -e $QT_DIR ]
then
  pushd /tmp
  curl -O https://gist.githubusercontent.com/DDRBoxman/9c7a2b08933166f4b61ed9a44b242609/raw/ef4de6c587c6bd7f50210eccd5bd51ff08e6de13/qt.rb
  brew install ./qt.rb
  popd
fi

echo Checking for SWIG
if [ ! -e /usr/local/Cellar/swig/ ]
then
  pushd /tmp
  curl -O https://gist.githubusercontent.com/DDRBoxman/4cada55c51803a2f963fa40ce55c9d3e/raw/572c67e908bfbc1bcb8c476ea77ea3935133f5b5/swig.rb
  brew install ./swig.rb
  popd
fi

echo Configuring and building
mkdir -p build
pushd build
cmake \
  -DENABLE_SPARKLE_UPDATER=ON \
  -DDISABLE_PYTHON=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=10.11 \
  -DQTDIR="$QT_DIR" \
  -DDepsPath="$OBSDEPS_DIR" \
  -DVLCPath="$VLC_PATH" \
  -DENABLE_VLC=ON \
  -DBUILD_BROWSER=ON \
  -DBROWSER_DEPLOY=ON \
  -DBUILD_CAPTIONS=ON \
  -DWITH_RTMPS=ON \
  -DCEF_ROOT_DIR="$CEF_ROOT_DIR" \
  -DLIBCAFFEINE_DIR="$LIBCAFFEINE_DIR" \
  ..
make -j
popd


cat <<EOT

============================================================

All done!

You may need to \`source ~/.zshrc\` to pick up new environment
variables before proceeding.

To start a new build:
  cd $PWD/build && make -j

Start the app with:
  cd $PWD/build/rundir/RelWithDebInfo/bin && ./obs

EOT
