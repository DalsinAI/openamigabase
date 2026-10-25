SELECT Customers.Name, count(Orders.id) AS Orders FROM Customers LEFT JOIN Orders ON Customers.id = Orders.customer GROUP BY Customers.Name ORDER BY Customers.Name
