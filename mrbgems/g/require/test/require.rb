assert('require test') do
  Kernel.respond_to? :require
end
assert('require test') do
  Kernel.respond_to? :load
end
