
# pkg-provides
pkgng plugin for querying which package will provide you a particular file

# Since february 2018, pkg-provides can be installed from FreeBSD and DragonFlyBSD packages.

[![asciicast](https://asciinema.org/a/funEHTv8VYyD5MBZzU9XGRwiS.png)](https://asciinema.org/a/funEHTv8VYyD5MBZzU9XGRwiS)

# How to build / install

## Requirements

  * libfetch (freebsd base )
  * libpcre  ( pkg install pcre )
  * libpkg   ( freebsd base )
  * uthash   ( pkg install uthash)

## Build

```
# make install
```

## Install
```
# make install
```

## Configure pkg to use plugins

/usr/local/etc/pkg.conf


```
PKG_PLUGINS_DIR = "/usr/local/lib/pkg/";
PKG_ENABLE_PLUGINS = true;
PLUGINS [ provides ]
```
Create the directory to store the provides database :

```
    mkdir -p /var/db/pkg/provides
```

## Check if the plugin is available 

```
# pkg help
Commands provided by plugins:
        provides       A plugin for querying which package provides a particular file
```
# Usage

Update the provides database
```
  pkg provides -u
```

Search for a file
```
  pkg provides firefox
```
```
  pkg provides bin/firefox
```
You can use the perl regexp in search requests

