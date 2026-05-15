// Notepatra palette preview — synthetic; no real data
namespace java  com.example.notepatra.demo
namespace py    notepatra.demo
namespace cpp   notepatra.demo

include "shared.thrift"

typedef i64 UserId
typedef string Email

enum Role {
  VIEWER = 1,
  EDITOR = 2,
  ADMIN  = 3,
}

struct Address {
  1: required string street,
  2: required string city,
  3: optional string zip,
  4: optional string country = "Exampleland",
}

struct User {
  1:  required UserId        id,
  2:  required string        display_name,
  3:  required Email         email,
  4:  required Role          role           = Role.VIEWER,
  5:  optional bool          active         = true,
  6:  optional i32           login_count    = 0,
  7:  optional double        score          = 0.0,
  8:  optional Address       address,
  9:  optional list<string>  tags,
  10: optional map<string,string> attributes,
  11: optional set<string>   roles_extra,
}

exception NotFound {
  1: required string message,
  2: optional UserId id,
}

exception PermissionDenied {
  1: required string message,
}

service UserService {
  User getUser(1: UserId id)
    throws (1: NotFound nf, 2: PermissionDenied pd),

  list<User> listUsers(1: i32 page_size, 2: string page_token),

  void deleteUser(1: UserId id)
    throws (1: NotFound nf),

  oneway void recordPing(1: UserId id),
}
