MRuby.each_target do
  file libfile("#{build_dir}/lib/libmruby") => libmruby.flatten do |t|
    archiver.run t.name, t.prerequisites
    open("#{build_dir}/lib/libmruby.flags.mak", 'w') do |f|
      f.puts 'CFLAGS = "%s"' % cc.all_flags.gsub('"', '\\"') 

      gem_flags = gems.map { |g| g.linker.flags }
      gem_library_paths = gems.map { |g| g.linker.library_paths }
      f.puts 'LDFLAGS = "%s"' % linker.all_flags(gem_library_paths, gem_flags).gsub('"', '\\"') 

      gem_libraries = gems.map { |g| g.linker.libraries }
      f.puts 'LIBS = "%s"' % linker.library_flags(gem_libraries).gsub('"', '\\"') 
    end
  end
end
