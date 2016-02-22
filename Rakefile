$LOAD_PATH << File.expand_path("../lib", __FILE__)

require "rake/extensiontask"
require "filter_bloom/version"

Rake::ExtensionTask.new("filter_impl") do |ext|
  ext.lib_dir = "lib/filter_bloom"
end

task :build do
  sh "gem build filter_bloom.gemspec"
end

task :release => :build do
  sh "gem push filter_bloom-#{BloomFilter::Version}.gem"
end 

task :clean do
  sh "git clean -xdf"
end
