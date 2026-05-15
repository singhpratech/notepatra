{ Notepatra palette preview — synthetic; no real data }
program NotepatraDemo;

uses
  SysUtils, Math;

const
  AppName    = 'Notepatra';
  AppVersion = '0.1.84';
  MaxItems   = 5;
  Pi64       = 3.141592653589793;

type
  TRole = (rViewer, rEditor, rAdmin);

  TUser = record
    Id:    Integer;
    Name:  string;
    Email: string;
    Role:  TRole;
    Score: Double;
  end;

  TUserArray = array[1..MaxItems] of TUser;

var
  Users: TUserArray;
  i: Integer;
  Total: Double;

function RoleName(R: TRole): string;
begin
  case R of
    rViewer: RoleName := 'viewer';
    rEditor: RoleName := 'editor';
    rAdmin:  RoleName := 'admin';
  end;
end;

procedure SeedUsers(var arr: TUserArray);
begin
  arr[1].Id := 1; arr[1].Name := 'Alice'; arr[1].Email := 'alice@example.com'; arr[1].Role := rAdmin;  arr[1].Score := 9.5;
  arr[2].Id := 2; arr[2].Name := 'Bob';   arr[2].Email := 'bob@example.com';   arr[2].Role := rEditor; arr[2].Score := 8.0;
  arr[3].Id := 3; arr[3].Name := 'Carol'; arr[3].Email := 'carol@example.com'; arr[3].Role := rViewer; arr[3].Score := 7.2;
  arr[4].Id := 4; arr[4].Name := 'Dave';  arr[4].Email := 'dave@example.com';  arr[4].Role := rEditor; arr[4].Score := 6.1;
  arr[5].Id := 5; arr[5].Name := 'Eve';   arr[5].Email := 'eve@example.com';   arr[5].Role := rAdmin;  arr[5].Score := 9.8;
end;

begin
  WriteLn(AppName, ' v', AppVersion);
  SeedUsers(Users);

  Total := 0.0;
  for i := 1 to MaxItems do
  begin
    WriteLn(i, '. ', Users[i].Name, ' <', Users[i].Email, '> [', RoleName(Users[i].Role), '] score=', Users[i].Score:0:2);
    Total := Total + Users[i].Score;
  end;

  if MaxItems > 0 then
    WriteLn('Average score: ', (Total / MaxItems):0:2)
  else
    WriteLn('No users.');

  WriteLn('pi = ', Pi64:0:6);
end.
