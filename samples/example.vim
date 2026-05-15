" Notepatra palette preview — synthetic; no real data
" A small Vim script exercising common syntax categories.

if exists('g:loaded_notepatra_demo')
  finish
endif
let g:loaded_notepatra_demo = 1

" Options
set nocompatible
set number
set relativenumber
set expandtab
set shiftwidth=4
set tabstop=4
set softtabstop=4
set ignorecase
set smartcase
set hlsearch
set incsearch
set termguicolors

" Global variables
let g:notepatra_demo_author = 'Alice'
let g:notepatra_demo_email  = 'alice@example.com'
let s:files = ['one.txt', 'two.txt', 'three.txt']

" Function with bang to allow re-sourcing
function! NotepatraGreet(name) abort
  let l:msg = 'Hello, ' . a:name . '!'
  echohl WarningMsg
  echom l:msg
  echohl None
  return l:msg
endfunction

function! s:CountLines() abort
  let l:n = line('$')
  return l:n
endfunction

command! -nargs=1 Greet call NotepatraGreet(<q-args>)
command! LineCount echo s:CountLines() . ' lines'

" Mappings
nnoremap <silent> <leader>g :call NotepatraGreet('world')<CR>
inoremap jk <Esc>
vnoremap <leader>y "+y

" Autocommands
augroup NotepatraDemo
  autocmd!
  autocmd BufReadPost *.md  setlocal spell
  autocmd BufWritePre *.py  silent! %s/\s\+$//e
  autocmd FileType   yaml setlocal shiftwidth=2 tabstop=2
augroup END

" Range example
function! s:UpperRange() range
  execute a:firstline . ',' . a:lastline . 's/\<.\+\>/\U&/g'
endfunction
command! -range Upper <line1>,<line2>call s:UpperRange()

if has('nvim')
  echo 'Running under Neovim'
else
  echo 'Running under Vim'
endif
