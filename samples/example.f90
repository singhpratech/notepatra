! Notepatra palette preview — synthetic; no real data

module greetings
  implicit none
  private
  public :: greet, square, user_t

  type :: user_t
     character(len=32) :: name
     character(len=64) :: email
     real              :: score
     logical           :: active
  end type user_t

contains

  subroutine greet(name)
    character(len=*), intent(in) :: name
    print '(A,A,A)', 'Hello, ', trim(name), '!'
  end subroutine greet

  function square(x) result(y)
    real, intent(in) :: x
    real             :: y
    y = x * x
  end function square

end module greetings


program notepatra_demo
  use greetings
  implicit none

  integer, parameter :: n = 3
  integer            :: i
  real               :: total
  real, dimension(5) :: data = [ 1.0, 2.5, 3.75, 4.0, 5.25 ]
  type(user_t)       :: users(n)
  character(len=32)  :: name

  users(1) = user_t('Alice', 'alice@example.com', 9.5, .true.)
  users(2) = user_t('Bob',   'bob@example.com',   8.0, .true.)
  users(3) = user_t('Carol', 'carol@example.com', 7.2, .false.)

  do i = 1, n
     if (users(i)%active) then
        name = users(i)%name
        call greet(trim(name))
        print '(A,F4.1)', '  score = ', users(i)%score
     else
        print '(A,A)', 'skipped (inactive): ', trim(users(i)%name)
     end if
  end do

  total = 0.0
  do i = 1, size(data)
     total = total + data(i)
  end do
  print '(A,F6.2)', 'sum   = ', total
  print '(A,F6.2)', 'mean  = ', total / real(size(data))
  print '(A,F6.2)', '3^2   = ', square(3.0)

end program notepatra_demo
