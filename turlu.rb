txtt = 'Baroum vraiment vite ici'
p txtt
txtt = txtt.gsub("vite", "lent")
p txtt
p "Hello"

puts "### Sample.1 ###"
str = "this is regexp"
rp = Regexp.new("^this is regexp")
p str == str # => true
p rp.match(str) # => #<MatchData "this is regexp">
p Regexp.last_match[0] #=> "this is regexp"

puts ""
puts "### Sample.2 ###"
str = "THIS IS REGEXP"
t1 = Regexp.compile("^this is regexp", Regexp::IGNORECASE)
t = t1.match(str)
puts t # => THIS IS REGEXP