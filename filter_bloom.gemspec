$LOAD_PATH.unshift File.expand_path('../lib', __FILE__)
require 'filter_bloom/version'

Gem::Specification.new do |s|
  s.name = "filter_bloom"
  s.version = BloomFilter::Version

  s.authors = ["Joseph Tibbertsma"]
  s.description = "Implementation of BloomFilter in Ruby C API. Provides a simple interface to create bloom filters and test ruby objects against the filter."
  s.summary = "Implementation of BloomFilter in Ruby C API."
  s.email = ["josephtibbertsma@gmail.com"]
  s.files = `git ls-files`.split($/)
  s.extensions = ["ext/filter_impl/extconf.rb"]
  s.licenses = ["MIT"]
  s.homepage = "http://github.com/jtibbertsma/bloom_filter"

  s.required_ruby_version = '>= 2.1.0'

  s.add_development_dependency(%q<rdoc>, ["~> 4.0"])
  s.add_development_dependency(%q<rake-compiler>, ["~> 0.8.0"])
end
