SELECT Customers.Name, sum(Orders.Total) AS SumOfTotal FROM Customers JOIN Orders ON Customers.id = Orders.customer GROUP BY Customers.Name HAVING sum(Orders.Total) > 10000 ORDER BY Customers.Name
