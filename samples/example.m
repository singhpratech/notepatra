% Notepatra palette preview — synthetic; no real data
% MATLAB / Octave demo script.

function demo()
    appName    = 'Notepatra';
    appVersion = '0.1.84';
    fprintf('%s v%s\n', appName, appVersion);

    users = struct( ...
        'name',  {'Alice', 'Bob', 'Carol'}, ...
        'email', {'alice@example.com', 'bob@example.com', 'carol@example.com'}, ...
        'score', {9.5, 8.0, 7.2}, ...
        'active', {true, true, false});

    for k = 1:numel(users)
        u = users(k);
        if u.active
            fprintf('%d. %s <%s> score=%.2f\n', k, u.name, u.email, u.score);
        else
            fprintf('   (skipped inactive: %s)\n', u.name);
        end
    end

    % Anonymous function + vectorised ops
    square = @(x) x .^ 2;
    cube   = @(x) x .^ 3;

    xs = 1:10;
    ys = square(xs);
    zs = cube(xs);

    fprintf('mean(square 1..10) = %.2f\n', mean(ys));
    fprintf('sum(cube  1..10)   = %.2f\n', sum(zs));

    % Cell array
    colors = {'red', 'green', 'blue'};
    for c = 1:length(colors)
        disp(colors{c});
    end

    % Matrix + plot (suppressed)
    A = [1 2 3; 4 5 6; 7 8 10];
    if det(A) ~= 0
        fprintf('det(A) = %.2f, invertible\n', det(A));
    else
        fprintf('det(A) = 0, singular\n');
    end

    % Plot is harmless under -nodisplay
    try
        figure('Visible','off');
        plot(xs, ys, '-o');
        title('y = x^2');
        xlabel('x'); ylabel('y');
        close;
    catch err
        fprintf('plot skipped: %s\n', err.message);
    end
end

demo();
