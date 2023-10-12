
# How to generate your pkg-provides database using poudriere hooks

This directory contains:
 * The pkgrepo.sh hook script to generate the pkg-provides database before signing the repo
 * A ruby script who parses your packages to extract the plist and generate a pkg-provides database

## requirements

List of packages required to generate the pkg-provides database
 * rubygem-zstd-ruby
 * rubygem-ruby-xz

## Benchmarking

    CPU: Intel(R) Core(TM) i5-6500 CPU @ 3.20GHz (3200.00-MHz K8-class CPU)
    hw.physmem: 34185838592
    hw.usermem: 22954500096
    hw.realmem: 34359738368

For a subset of 245 packages it takes about 66 seconds to generate the pkg-provides database with 4 threads.

The script uses the poudriere PARALLEL_JOBS environment variable to parallelize the process.

## How to make it works

    1. cp pkgrepo.sh provides-db-gen.rb /usr/local/etc/poudriere.d/hooks/
    2. chmod +x /usr/local/etc/poudriere.d/hooks/pkgrepo.sh /usr/local/etc/poudriere.d/hooks/provides-db-gen.rb
    3. run a bulk build with poudriere!
    4. the provides.db.xz file will be stored in the package direcory at the same place as packagesite.pkg

## Integrate with the pkg-provides plugin

The pkg-plugin uses a set of environment variables to overload some default variables:

|Variable | Default value | Meaning |
|-------- | ------------- | ------- |
| PROVIDES_SRV      | https://pkg-provides.osorio.me | server where the pkg-provides database is stored |
| PROVIDES_FILEPATH | v3/FreeBSD/12:amd64 (for a FreeBSD 12 amd64) | location of the provides.db.xz file |


If the following command works, you are fine.
  fetch ${PROVIDES_SRV}/${PROVIDES_FILEPATH}/provides.db.xz
