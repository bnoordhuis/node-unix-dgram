def copy_files():
  # waf is such a POS...
  import shutil
  shutil.copy('src/unix_dgram.js', 'build/default')

def set_options(ctx):
  ctx.tool_options('compiler_cxx')

def configure(ctx):
  ctx.check_tool('compiler_cxx')
  ctx.check_tool('node_addon')

def build(ctx):
  t = ctx.new_task_gen('cxx', 'shlib', 'node_addon')
  t.target = 'unix_dgram'
  t.source = 'src/unix_dgram.cc'
  ctx.install_files('${PREFIX}', 'src/*.js')
  copy_files()
