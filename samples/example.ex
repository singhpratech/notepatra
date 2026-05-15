# Notepatra palette preview - synthetic; no real data
# Exercises: defmodule, def, pattern matching, |> pipe, atoms, lists, maps,
# GenServer skeleton, control flow.

defmodule Samples.User do
  @moduledoc "Synthetic user record."

  defstruct [:id, :name, :email, status: :pending]

  @type t :: %__MODULE__{
          id: integer(),
          name: String.t(),
          email: String.t(),
          status: :pending | :active | :archived
        }

  def greet(%__MODULE__{name: n, email: e}), do: "hello #{n} <#{e}>"
end

defmodule Samples.Classify do
  def call(nil), do: "nil"
  def call(value) when is_integer(value) and value < 0, do: "neg:#{value}"
  def call(value) when is_integer(value), do: "int:#{value}"
  def call(value) when is_binary(value), do: "str:#{value}"
  def call(%Samples.User{name: n}), do: "user:#{n}"
  def call(_), do: "unknown"
end

defmodule Samples.Repo do
  use GenServer

  @pi 3.14159
  @max_retries 0x10

  def start_link(_opts \\ []), do: GenServer.start_link(__MODULE__, %{}, name: __MODULE__)

  @impl true
  def init(state), do: {:ok, state}

  @impl true
  def handle_call({:add, %Samples.User{} = u}, _from, state) do
    {:reply, :ok, Map.put(state, u.id, u)}
  end

  def handle_call(:all, _from, state), do: {:reply, Map.values(state), state}

  def constants, do: %{pi: @pi, retries: @max_retries}
end

defmodule Samples.Main do
  alias Samples.{User, Classify}

  def run do
    users = [
      %User{id: 1, name: "Alice", email: "alice@example.com", status: :active},
      %User{id: 2, name: "Bob", email: "bob@example.org"}
    ]

    greetings =
      users
      |> Enum.filter(fn u -> String.contains?(u.email, "@example.") end)
      |> Enum.map(&User.greet/1)

    squares = for n <- 1..3, do: n * n
    counts = %{pending: 0, active: 2, archived: 1}

    IO.inspect(Samples.Repo.constants(), label: "constants")
    Enum.each(greetings, &IO.puts/1)
    IO.inspect(squares, label: "squares")
    IO.inspect(counts, label: "counts")
    Enum.each([-3, 42, "ok", nil, hd(users)], fn v -> IO.puts(Classify.call(v)) end)
  end
end

Samples.Main.run()
