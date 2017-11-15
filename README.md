# pkg-provides
pkgng plugin for querying which package provides a particular file

# How to build / install

## Requirements

  * libfetch (freebsd base )
  * libpcre  ( pkg install pcre )
  * libpkg   ( freebsd base )

## Build

```
# make install
```

## Install
```
# make install
```

## Configure pkg to use plugins

```
PKG_PLUGINS_DIR = "/usr/local/lib/pkg/plugins";
PKG_ENABLE_PLUGINS = true;
PLUGINS [ provides ]
PLUGINS_CONF_DIR = "/usr/local/etc/pkg/plugins";
```
Create the directory to store the provides database :

```
    mkdir -p /var/db/pkg/plugins
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


