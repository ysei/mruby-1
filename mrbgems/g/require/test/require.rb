assert('require test') do
  Kernel.respond_to? :require
end
assert('require test') do
  Kernel.respond_to? :load
end
assert('require test') do
    class Thing
    end

    a = "def hello()  \"Hello Thing!\" end"

    Thing.module_eval(a)

    Thing.new.hello() == "Hello Thing!"
end
