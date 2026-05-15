// Notepatra palette preview - synthetic; no real data
// Exercises: package, imports, struct + tags, methods, interfaces,
// goroutines, channels, defer, error handling, slice/map/array, control flow.

package main

import (
	"errors"
	"fmt"
	"sync"
)

const MaxWorkers = 4

type Status int

const (
	StatusPending Status = iota
	StatusActive
	StatusArchived
)

type User struct {
	ID    int    `json:"id"`
	Name  string `json:"name"`
	Email string `json:"email"`
}

type Greeter interface {
	Greet() string
}

func (u User) Greet() string {
	return fmt.Sprintf("hello %s <%s>", u.Name, u.Email)
}

func divide(a, b float64) (float64, error) {
	if b == 0 {
		return 0, errors.New("divide by zero")
	}
	return a / b, nil
}

func worker(id int, jobs <-chan int, results chan<- int, wg *sync.WaitGroup) {
	defer wg.Done()
	for j := range jobs {
		results <- j * j
		_ = id
	}
}

func main() {
	users := []User{
		{ID: 1, Name: "Alice", Email: "alice@example.com"},
		{ID: 2, Name: "Bob", Email: "bob@example.org"},
	}
	counts := map[Status]int{StatusPending: 0, StatusActive: 2, StatusArchived: 1}
	var fixed [3]int = [3]int{10, 20, 30}

	for _, u := range users {
		var g Greeter = u
		fmt.Println(g.Greet())
	}

	jobs := make(chan int, 5)
	results := make(chan int, 5)
	var wg sync.WaitGroup
	for w := 1; w <= 2; w++ {
		wg.Add(1)
		go worker(w, jobs, results, &wg)
	}
	for j := 1; j <= 5; j++ { jobs <- j }
	close(jobs)
	wg.Wait()
	close(results)

	if q, err := divide(10, 2); err == nil {
		fmt.Println("q =", q)
	}
	fmt.Println(counts, fixed)
}
