#!/usr/bin/env perl
# Notepatra palette preview - synthetic; no real data
# Exercises: use strict, my/our, scalar/array/hash, regex, subroutines,
# references, hash slices, heredoc, control flow.

use strict;
use warnings;
use feature qw(say);

our $PI         = 3.14159;
our $MAX_RETRY  = 0x10;

my @users = (
    { id => 1, name => 'Alice', email => 'alice@example.com' },
    { id => 2, name => 'Bob',   email => 'bob@example.org'  },
);

my %status_counts = (
    pending  => 0,
    active   => 2,
    archived => 1,
);

sub greet {
    my ($user) = @_;
    return "hello $user->{name} <$user->{email}>";
}

sub describe {
    my ($v) = @_;
    return 'undef' unless defined $v;
    if ($v =~ /^-?\d+$/) {
        return $v < 0 ? "neg:$v" : "int:$v";
    }
    return "str:$v";
}

sub email_ok {
    my ($addr) = @_;
    return $addr =~ /\A[a-z0-9._%+-]+\@example\.(?:com|org)\z/i;
}

my $intro = <<"END_INTRO";
Repository has @{[ scalar @users ]} users.
Pi is $PI; retries = $MAX_RETRY.
END_INTRO

say $intro;

for my $u (@users) {
    say greet($u) if email_ok($u->{email});
}

my @names = map { $_->{name} } @users;
my @upper = grep { /^[A-Z]/ } @names;
say "names: @names";
say "upper: @upper";

my @keys = qw(active pending);
my @slice = @status_counts{@keys};
say "slice = @slice";

while (my ($k, $v) = each %status_counts) {
    say "$k => $v";
}

for my $x (-3, 42, 'ok', undef) {
    say describe($x);
}
