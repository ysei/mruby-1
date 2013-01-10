#require 'plist'
load 'urlu.rb'
class Text
    def initialize(name)
        @name = name
        @text = ""
    end
    
    def name
        @name
    end
    
    def text
        @text
    end
    
    def set_text(txt)
        @text += txt
    end
    
    def addTurlututu
        @text += "turlututu"
    end
    
    def execute_with_proc(p)
        p.call(@x, @y)
    end
    
    def execute_with_yield(&b)
        yield @x, @y
    end
    
end
txtt = "Baroum vite ici"
txtt = txtt.gsub(/vite/, "lent")
p txtt

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
txtt = "Baroum vite ici"
txtt = txtt.gsub(/vite/, "lent")
p txtt

