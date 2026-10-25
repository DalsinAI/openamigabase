SELECT c.Town, max(o.Total) AS Largest, count(*) AS CountOfAll FROM Customers AS c JOIN Orders AS o ON c.id = o.customer GROUP BY c.Town ORDER BY c.Town
