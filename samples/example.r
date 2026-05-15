# Notepatra palette preview - synthetic; no real data
# Exercises: assignment <-, function, vector c(), data.frame, lapply,
# library(), <- vs =, control flow, S3-style methods.

library(stats)

PI <- 3.14159
MAX_RETRIES <- 16L

users <- data.frame(
    id    = c(1L, 2L, 3L),
    name  = c("Alice", "Bob", "Carol"),
    email = c("alice@example.com", "bob@example.org", "carol@example.org"),
    score = c(90.5, 75.0, NA),
    stringsAsFactors = FALSE
)

shapes <- list(
    list(kind = "circle", size = 1.5),
    list(kind = "square", size = 2.0),
    list(kind = "none",   size = 0.0)
)

area <- function(shape) {
    if (shape$kind == "circle") {
        PI * shape$size ^ 2
    } else if (shape$kind == "square") {
        shape$size ^ 2
    } else {
        0.0
    }
}

greet <- function(name, email = "unknown@example.com") {
    sprintf("hello %s <%s>", name, email)
}

classify <- function(value) {
    if (is.null(value) || is.na(value)) return("missing")
    if (is.numeric(value)) {
        if (value < 0) return(paste0("neg:", value))
        return(paste0("num:", value))
    }
    if (is.character(value)) return(paste0("str:", value))
    "unknown"
}

areas <- sapply(shapes, area)
labels <- mapply(greet, users$name, users$email)
totals <- vapply(users$score, function(s) ifelse(is.na(s), 0, s), numeric(1))

active <- subset(users, !is.na(score) & score > 70)
mean_score <- mean(users$score, na.rm = TRUE)

cat(sprintf("pi=%.5f retries=%d mean=%.2f\n", PI, MAX_RETRIES, mean_score))
cat("areas:", areas, "\n")
print(labels)
print(head(active))

for (v in list(-3, 42, "ok", NA)) {
    cat(classify(v), "\n")
}

result <- lapply(seq_len(3), function(i) i * i)
print(result)
