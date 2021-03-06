require 'forwardable'

module MRuby
  class Command
    include Rake::DSL
    extend Forwardable
    def_delegators :@build, :filename, :objfile, :libfile, :exefile
    attr_accessor :build, :command

    def initialize(build)
      @build = build
    end

    # clone is deep clone without @build
    def clone
      target = super
      excepts = %w(@build)
      instance_variables.each do |attr|
        unless excepts.include?(attr.to_s)
          val = Marshal::load(Marshal.dump(instance_variable_get(attr))) # deep clone
          target.instance_variable_set(attr, val)
        end
      end
      target
    end

    private
    def _run(options, params={})
      sh build.filename(command) + ' ' + ( options % params )
    end
  end
 
  class Command::Compiler < Command
    attr_accessor :flags, :include_paths, :defines, :source_exts
    attr_accessor :compile_options, :option_define, :option_include_path, :out_ext

    def initialize(build, source_exts=[])
      super(build)
      @command = ENV['CC'] || 'gcc'
      @flags = [ENV['CFLAGS'] || []]
      @source_exts = source_exts
      @include_paths = ["#{build.root}/include"]
      @defines = %w(DISABLE_GEMS)
      @option_include_path = '-I%s'
      @option_define = '-D%s'
      @compile_options = "%{flags} -MMD -o %{outfile} -c %{infile}"
    end

    def all_flags(_defineds=[], _include_paths=[], _flags=[])
      define_flags = [defines, _defineds].flatten.map{ |d| option_define % d }
      include_path_flags = [include_paths, _include_paths].flatten.map{ |f| option_include_path % filename(f) }
      [flags, define_flags, include_path_flags, _flags].flatten.join(' ')
    end
    
    def run(outfile, infile, _defineds=[], _include_paths=[], _flags=[])
      FileUtils.mkdir_p File.dirname(outfile)
      define_flags = [defines, _defineds].flatten.map{ |d| option_define % d }
      include_path_flags = [include_paths, _include_paths, File.dirname(infile)].flatten.map do |f|
        option_include_path % filename(f)
      end
      _pp "CC #{filename(infile)} > #{filename(outfile)}"
      _run compile_options, { :flags => (flags + define_flags + include_path_flags + _flags).join(' '),
                              :infile => filename(infile), :outfile => filename(outfile) }
    end

    def define_rules(build_dir, source_dir='')
      @out_ext = build.exts.object

      generated_file_matcher = Regexp.new("^#{build_dir}/(.*)#{Regexp.escape out_ext}$")
      source_exts.each do |ext, compile|
        rule generated_file_matcher => [
          proc { |file|
            file.sub(generated_file_matcher, "#{source_dir}\\1#{ext}")
          },
          proc { |file|
            get_dependencies(file)
          }
        ] do |t|
          run t.name, t.prerequisites.first
        end

        rule generated_file_matcher => [
          proc { |file|
            file.sub(generated_file_matcher, "#{build_dir}/\\1#{ext}")
          },
          proc { |file|
            get_dependencies(file)
          }
        ] do |t|
          run t.name, t.prerequisites.first
        end
      end
    end

    private
    def get_dependencies(file)
      file = file.ext('d') unless File.extname(file) == '.d'
      if File.exists?(file)
        File.read(file).gsub("\\\n ", "").scan(/^\S+:\s+(.+)$/).flatten.map {|s| s.split(' ') }.flatten
      else
        []
      end
    end
  end

  class Command::Linker < Command
    attr_accessor :flags, :libraries, :library_paths
    attr_accessor :link_options, :option_library, :option_library_path

    def initialize(build)
      super
      @command = ENV['LD'] || 'gcc'
      @flags = (ENV['LDFLAGS'] || [])
      @libraries = []
      @library_paths = []
      @option_library = '-l%s'
      @option_library_path = '-L%s'
      @link_options = "%{flags} -o %{outfile} %{objs} %{libs}"
    end

    def all_flags(_library_paths=[], _flags=[])
      library_path_flags = [library_paths, _library_paths].flatten.map{ |f| option_library_path % filename(f) }
      [flags, library_path_flags, _flags].flatten.join(' ')
    end

    def library_flags(_libraries)
      [libraries, _libraries].flatten.reverse.map{ |d| option_library % d }.join(' ')
    end

    def run(outfile, objfiles, _libraries=[], _library_paths=[], _flags=[])
      FileUtils.mkdir_p File.dirname(outfile)
      library_flags = [libraries, _libraries].flatten.reverse.map{ |d| option_library % d }
      library_path_flags = [library_paths, _library_paths].flatten.map{ |f| option_library_path % filename(f) }
      _pp "LD #{filename(outfile)}"
      _run link_options, { :flags => (flags + library_path_flags + _flags).join(' '),
                           :outfile => filename(outfile) , :objs => filename(objfiles).join(' '),
                           :libs => library_flags.join(' ') }
    end
  end

  class Command::Archiver < Command
    attr_accessor :archive_options

    def initialize(build)
      super
      @command = ENV['AR'] || 'ar'
      @archive_options = 'rs %{outfile} %{objs}'
    end

    def run(outfile, objfiles)
      FileUtils.mkdir_p File.dirname(outfile)
      _pp "AR #{filename(outfile)}"
      _run archive_options, { :outfile => filename(outfile), :objs => filename(objfiles).join(' ') }
    end
  end

  class Command::Yacc < Command
    attr_accessor :compile_options

    def initialize(build)
      super
      @command = 'bison'
      @compile_options = '-o %{outfile} %{infile}'
    end

    def run(outfile, infile)
      FileUtils.mkdir_p File.dirname(outfile)
      _pp "YACC #{filename(infile)} > #{filename(outfile)}"
      _run compile_options, { :outfile => filename(outfile) , :infile => filename(infile) }
    end
  end

  class Command::Gperf < Command
    attr_accessor :compile_options

    def initialize(build)
      super
      @command = 'gperf'
      @compile_options = '-L ANSI-C -C -p -j1 -i 1 -g -o -t -N mrb_reserved_word -k"1,3,$" %{infile} > %{outfile}'
    end

    def run(outfile, infile)
      FileUtils.mkdir_p File.dirname(outfile)
      _pp "GPERF #{filename(infile)} > #{filename(outfile)}"
      _run compile_options, { :outfile => filename(outfile) , :infile => filename(infile) }
    end
  end

  class Command::Git < Command
    attr_accessor :flags
    attr_accessor :clone_options

    def initialize(build)
      super
      @command = 'git'
      @flags = []
      @clone_options = "clone %{flags} %{url} %{dir}"
    end

    def run_clone(dir, url, _flags = [])
      _pp "GIT #{url} > #{filename(dir)}"
      _run clone_options, { :flags => [flags, _flags].flatten.join(' '), :url => url, :dir => filename(dir) }
    end
  end

  class Command::Mrbc < Command
    def initialize(build)
      super
      @command = nil
      @compile_options = "-B%{funcname} -o- -"
    end

    def run(out, infiles, funcname)
      @command ||= @build.mrbcfile
      IO.popen("#{filename @command} #{@compile_options % {:funcname => funcname}}", 'r+') do |io|
        [infiles].flatten.each do |f|
          _pp "  MRBC #{f}"
          io.write IO.read(f)
        end
        io.close_write
        out.puts io.read
      end
    end
  end
end
