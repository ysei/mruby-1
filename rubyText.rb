require 'urlu.rb'
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
txtt1 = txtt.gsub(/vite/, "lent")
RubyText::print("Test regexp "+txtt1)

if(/vite/ =~ txtt)
    RubyText::print("OK matches vite")
else
    RubyText::print("KO does not match vite")
end

RubyText::print("Calling CoreText::simple()")
RubyText::simple()

RubyText::print("Calling CoreText::TextAccess.new()")
f = RubyText::TextAccess.new("hello vite fait")

RubyText::print("f = "+f.get_text() )

#f.get_text().
a = Text.new("Hello")
a.set_text("berlu")
RubyText::print(a.text)




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

