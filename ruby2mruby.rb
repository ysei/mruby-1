#!/usr/bin/env ruby

require 'rubygems'
require 'treetop'
p "*************************************************"

Treetop.load 'ruby2mruby.treetop'
# or just:
# require 'my_grammar'                     # This works because Polyglot hooks "require" to find and load Treetop files

p "*************************************************"

parser = TranslationParser.new
p "*************************************************"
puts parser.parse('h (int a')         # => Treetop::Runtime::SyntaxNode
puts parser.parse('h (int a, double b')  # => nil

path = ARGV[0]

file = File.open(path, "rb")
contents = file.read
#contents.split(/\n/).each {|line| p line}


