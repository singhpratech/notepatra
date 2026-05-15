// Notepatra palette preview - synthetic; no real data
// Exercises (Apex): public class, methods, SOQL inline query, @future,
// trigger skeleton, control flow.

public with sharing class UserService {

    public static final Decimal PI = 3.14159;
    public static final Integer MAX_RETRIES = 16;

    public enum Status { PENDING, ACTIVE, ARCHIVED }

    public class UserDTO {
        public Id userId;
        public String name;
        public String email;
        public Status status;

        public UserDTO(String n, String e, Status s) {
            this.name = n;
            this.email = e;
            this.status = s;
        }

        public String greet() {
            return 'hello ' + this.name + ' <' + this.email + '>';
        }
    }

    public static List<UserDTO> loadActive() {
        List<UserDTO> result = new List<UserDTO>();
        // Synthetic SOQL: filter to active Contacts; data is placeholder only.
        for (Contact c : [
            SELECT Id, Name, Email
            FROM Contact
            WHERE Email LIKE '%@example.%'
            ORDER BY Name ASC
            LIMIT 50
        ]) {
            UserDTO u = new UserDTO(c.Name, c.Email, Status.ACTIVE);
            u.userId = c.Id;
            result.add(u);
        }
        return result;
    }

    @future(callout=false)
    public static void touchAsync(Set<Id> ids) {
        if (ids == null || ids.isEmpty()) {
            return;
        }
        List<Contact> toUpdate = [SELECT Id FROM Contact WHERE Id IN :ids];
        for (Contact c : toUpdate) {
            c.Description = 'touched';
        }
        if (!toUpdate.isEmpty()) {
            update toUpdate;
        }
    }

    public static String classify(Object value) {
        if (value == null) return 'null';
        if (value instanceof Integer) {
            Integer n = (Integer) value;
            return n < 0 ? 'neg:' + n : 'int:' + n;
        }
        if (value instanceof String) return 'str:' + (String) value;
        if (value instanceof UserDTO) return 'user:' + ((UserDTO) value).name;
        return 'unknown';
    }

    public static void demo() {
        UserDTO a = new UserDTO('Alice', 'alice@example.com', Status.ACTIVE);
        UserDTO b = new UserDTO('Bob', 'bob@example.org', Status.PENDING);
        System.debug('pi=' + PI + ' retries=' + MAX_RETRIES);
        System.debug(a.greet());
        System.debug(b.greet());
        for (Object v : new List<Object>{ -3, 42, 'ok', null, a }) {
            System.debug(classify(v));
        }
    }
}
