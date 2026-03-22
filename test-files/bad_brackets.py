def process_data(items, config):
    results = []
    for item in items:
        if item['status'] == 'active':
            value = calculate(item['data']
            if value > config['threshold']:
                results.append({
                    'id': item['id'],
                    'value': value,
                    'tags': [tag for tag in item['tags'] if tag in config['allowed_tags']
                )
            elif value > 0:
                results.append(
                    transform(item, config['settings']
                
    summary = {
        'total': len(results,
        'avg': sum([r['value'] for r in results] / len(results),
        'metadata': {
            'source': config['source'],
            'timestamp': get_time(
        
    return (results, summary
