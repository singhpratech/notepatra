-- Notepatra palette preview - synthetic; no real data
-- Exercises: DDL (CREATE TABLE), DML (INSERT/UPDATE/DELETE), SELECT with JOIN,
-- CTE (WITH), window functions (OVER PARTITION BY ROW_NUMBER), MERGE, TRY/CATCH,
-- types (NVARCHAR, DATETIME2, UNIQUEIDENTIFIER), system funcs, comment styles.
/* Block comment: T-SQL primary dialect. */

CREATE TABLE dbo.Users (
    UserId      UNIQUEIDENTIFIER NOT NULL DEFAULT NEWID() PRIMARY KEY,
    Name        NVARCHAR(64)     NOT NULL,
    Email       NVARCHAR(128)    NOT NULL,
    Status      NVARCHAR(16)     NOT NULL DEFAULT N'pending',
    CreatedAt   DATETIME2(3)     NOT NULL DEFAULT SYSUTCDATETIME(),
    Score       DECIMAL(10, 2)   NULL
);

CREATE TABLE dbo.Orders (
    OrderId     INT IDENTITY(1,1) PRIMARY KEY,
    UserId      UNIQUEIDENTIFIER NOT NULL,
    Amount      DECIMAL(12, 2)   NOT NULL,
    PlacedAt    DATETIME2(3)     NOT NULL DEFAULT SYSUTCDATETIME()
);

INSERT INTO dbo.Users (Name, Email, Status, Score) VALUES
    (N'Alice', N'alice@example.com', N'active',  90.50),
    (N'Bob',   N'bob@example.org',   N'active',  75.00),
    (N'Carol', N'carol@example.org', N'pending', NULL);

UPDATE dbo.Users SET Status = N'archived' WHERE Score IS NULL;
DELETE FROM dbo.Orders WHERE Amount <= 0;

;WITH RankedOrders AS (
    SELECT
        o.OrderId,
        o.UserId,
        o.Amount,
        ROW_NUMBER() OVER (PARTITION BY o.UserId ORDER BY o.Amount DESC) AS RowNum,
        SUM(o.Amount) OVER (PARTITION BY o.UserId) AS UserTotal
    FROM dbo.Orders AS o
)
SELECT
    u.Name,
    u.Email,
    COALESCE(r.UserTotal, 0)            AS Total,
    ISNULL(COUNT(r.OrderId), 0)         AS OrderCount
FROM dbo.Users AS u
LEFT JOIN RankedOrders AS r ON r.UserId = u.UserId AND r.RowNum = 1
WHERE u.Status = N'active'
GROUP BY u.Name, u.Email, r.UserTotal
HAVING COALESCE(r.UserTotal, 0) >= 0
ORDER BY Total DESC;

BEGIN TRY
    MERGE dbo.Users AS tgt
    USING (VALUES
        (N'Alice', N'alice@example.com', 95.00),
        (N'Dave',  N'dave@example.org',  60.00)
    ) AS src (Name, Email, Score)
       ON tgt.Email = src.Email
    WHEN MATCHED THEN
        UPDATE SET tgt.Score = src.Score
    WHEN NOT MATCHED BY TARGET THEN
        INSERT (Name, Email, Score) VALUES (src.Name, src.Email, src.Score);
END TRY
BEGIN CATCH
    SELECT ERROR_NUMBER() AS ErrNo, ERROR_MESSAGE() AS ErrMsg;
END CATCH;
