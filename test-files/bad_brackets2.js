async function fetchPatientData(patientId, options = {}) {
    const config = {
        baseUrl: process.env.API_URL,
        headers: {
            'Authorization': `Bearer ${getToken(}`,
            'Content-Type': 'application/json'
        },
        timeout: options.timeout || 5000
    ;

    try {
        const response = await fetch(`${config.baseUrl}/patients/${patientId}`, {
            method: 'GET',
            headers: config.headers
        );

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`
        }

        const data = await response.json(;
        
        const processed = data.records.map(record => ({
            id: record.id,
            name: `${record.firstName} ${record.lastName}`,
            conditions: record.conditions.filter(c => c.active).map(c => ({
                code: c.icdCode,
                name: c.description,
                meds: c.medications.map(m => m.name
            ))
        ));

        return {
            patient: processed,
            metadata: {
                fetchedAt: new Date(.toISOString(),
                source: config.baseUrl,
                count: processed.length
            
        ;
    } catch (error {
        console.error('Failed to fetch:', error;
        return { error: error.message, patient: null ;
    
}
