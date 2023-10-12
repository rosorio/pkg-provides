#!/usr/bin/env ruby
require 'zstd-ruby'
require 'xz'
require 'json'
require 'rubygems/package'

$wlock = Mutex.new
$count = 0

def extract_manifest_file(path)
 comp = File.open(path, "rb"){|f| f.read}
 begin
    uncompressed =  StringIO.new(Zstd.decompress(comp))
 rescue
    uncompressed = StringIO.new(XZ.decompress(comp))
 end
  Gem::Package::TarReader.new(uncompressed) do |tar|
        tar.each do |entry|
            if entry.full_name == "+MANIFEST"
                StringIO.new(entry.read).each do |line|
                    return line
                end
            end
        end
  end
end

def process_file(path)
    dump = extract_manifest_file(path)
    json = JSON.parse(dump)
    if json.key?("files")
         json["files"].keys.each do |file|
            puts json["name"] + "*" + file
         end
    end
    $wlock.synchronize {
        $count -= 1
    }
end


$threads = []
$maxproc = ARGV[1].to_i

Dir[ARGV[0] + '/*.pkg'].each {|f|
    sleep(1) until ($count < $maxproc)
    $wlock.synchronize {
        if ($count < $maxproc)
            puts "Star proc"
            $count += 1
            $threads << Thread.new { process_file(f) }
        end
    }
}

$threads.each { |thr| thr.join }
